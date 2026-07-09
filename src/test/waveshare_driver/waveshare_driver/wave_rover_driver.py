#!/usr/bin/env python3

# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Serial motion driver for the Waveshare WAVE ROVER.

Subscribes to ``/cmd_vel`` (``geometry_msgs/Twist``); a 20 Hz control tick mixes
the linear/angular targets into per-side commands (linear: dead-zone-lifted
feedforward; angular: feedforward + gyro yaw-rate PI, see ``ang_kp``) and sends
them to the ESP32 "General Driver for Robots" board as JSON over UART, e.g.::

    {"T":1,"L":0.25,"R":0.25}

The base WAVE ROVER has no wheel encoders, so ``L``/``R`` are *signed PWM
fractions* (``-output_max .. +output_max``), not true velocities. Rather than
pure diff-drive kinematics (which, with a ~0.12 m track, makes rotation map to a
tiny wheel differential that stalls the motors), we normalize the linear and
angular parts independently by ``max_linear_speed`` / ``max_angular_speed`` and
arcade-mix them into left/right. Tune those two so commanded motion and observed
motion roughly agree.

A watchdog stops the motors if ``/cmd_vel`` goes quiet for ``cmd_timeout``
seconds, and the motors are stopped on shutdown.

The board replies to ``{"T":126}`` polls with raw IMU frames on the same UART
(continuous base-info streaming is OFF by default in firmware, and its frame
carries only fused r/p/y anyway). A background reader thread parses replies and
publishes ``sensor_msgs/Imu`` on ``imu/data_raw`` (raw/unfused: angular velocity
+ linear acceleration, no orientation estimate); the poll rate is the IMU rate.
The firmware emits acceleration in milli-g and angular rate in deg/s, which are
converted to SI (m/s^2, rad/s); a measured gyro zero-rate bias is then subtracted.
See ``test_utilities/wave_rover_serial_teleop.py`` for the standalone reference
reader this mirrors.
"""

from collections import deque
import json
import math
import statistics
import threading
import time

from geometry_msgs.msg import Twist
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Imu

try:
    import serial
except ImportError as exc:  # pragma: no cover - surfaced clearly at runtime
    raise ImportError(
        'pyserial is required by waveshare_driver. Install it with '
        '`rosdep install` (python3-serial) or `pip install pyserial`.'
    ) from exc


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


class WaveRoverDriver(Node):
    """Bridges ``/cmd_vel`` to the WAVE ROVER serial JSON protocol."""

    def __init__(self) -> None:
        super().__init__('wave_rover_driver')

        # Serial link.
        self.declare_parameter('serial_port', '/dev/serial0')
        self.declare_parameter('baud', 115200)
        # Command scaling. The base WAVE ROVER has no encoders, so L/R are open-loop
        # PWM fractions -- there is no true velocity mapping. We therefore scale the
        # linear and angular parts of the twist *independently* by their own max
        # speeds and mix them (arcade style). Scaling them jointly through pure
        # diff-drive kinematics makes rotation map to a tiny wheel-speed differential
        # (track width is only ~0.12 m), which lands below the motor dead-zone and
        # leaves the rover unable to turn under normal teleop rates.
        # Calibration gains: the twist is normalized by these before the dead-zone
        # lift, so they set the commanded->actual velocity slope in the low band
        # nav2 uses (NOT the physical top speed). Bench fit (2026-07-16):
        # linear ~6 m/s per unit PWM above the floor -> max_lin ~= 2.4.
        # Angular response near the scrub floor is very steep (~20 rad/s per unit
        # PWM: p=0.20 -> 0.31 rad/s but p=0.27 -> 1.5 rad/s), so the usable
        # in-place band is p ~= 0.20..0.23 and the gain must be large:
        # w=0.75 rad/s should land at p ~= 0.223 -> max_ang ~= 9.0.
        # Raise a value if measured motion overshoots the command; lower if under.
        self.declare_parameter('max_linear_speed', 2.4)
        self.declare_parameter('max_angular_speed', 9.0)
        self.declare_parameter('output_max', 0.5)
        # Friction dead-zone compensation, per AXIS (not per wheel). Bench-calibrated
        # on this unit (2026-07-16, raw {"T":1} steps vs rf2o velocity / gyro rate):
        #   straight line: breaks free at PWM ~0.10, sustains down to ~0.10
        #                  (0.045 m/s crawl); static ~= kinetic.
        #   in-place spin: breaks free at ~0.22 (CCW, the sticky direction),
        #                  sustains to ~0.20 (0.31 rad/s); scrub-dominated.
        # A single per-wheel dead-zone cannot represent that split: sized for
        # rotation (0.22) it over-drives straight motion 2x (goal overshoot, no
        # approach deceleration) and crushes the L/R differential (no steering
        # authority). Instead the linear and angular twist components are each
        # remapped onto [deadband, output_max] BEFORE arcade mixing, so every
        # nonzero command produces motion on its own axis with the right floor.
        # Set a deadband to 0.0 to disable compensation on that axis.
        self.declare_parameter('lin_deadband', 0.10)
        self.declare_parameter('ang_deadband', 0.20)
        # While translating, the wheels are already rolling and steering
        # differential does not fight full static scrub, so the angular dead-zone
        # washes out linearly with linear throttle: at |lin PWM| >= this value the
        # angular lift floor is 0 (pure proportional differential). Prevents small
        # path-curvature corrections from slamming in a +-0.20 differential while
        # driving. Set large (> output_max) to keep the full floor always.
        self.declare_parameter('ang_deadband_washout', 0.25)
        # Closed-loop yaw rate (gyro PI). Bench data (2026-07-16) shows the
        # in-place rotation response is nearly a STEP: p<=0.20 -> no motion,
        # p=0.213 -> 1.2 rad/s, p=0.225 -> 1.7 rad/s (direction-asymmetric).
        # No open-loop map can hold 0.3-0.75 rad/s across that ~0.01-wide knee,
        # so a 20 Hz PI trims the angular output against the measured gyro z:
        # it dithers across the friction knee and delivers the commanded rate on
        # average, and holds wz=0 while driving straight (veer correction).
        # The feedforward lift above seeds it. Set both gains to 0 (or
        # publish_imu:=false) for pure open-loop feedforward.
        self.declare_parameter('ang_kp', 0.05)
        self.declare_parameter('ang_ki', 0.30)
        self.declare_parameter('ang_integ_max', 0.15)
        self.declare_parameter('control_rate', 20.0)
        self.declare_parameter('cmd_type', 1)
        # Safety / timing.
        self.declare_parameter('cmd_timeout', 0.5)

        # IMU feedback. The board reports base-info frames on the same UART; a reader
        # thread parses them and publishes sensor_msgs/Imu on ``imu/data_raw``.
        self.declare_parameter('publish_imu', True)
        self.declare_parameter('imu_frame_id', 'imu_link')
        self.declare_parameter('imu_poll_rate', 20.0)
        # Unit conversions: the QMI8658 firmware emits accel in milli-g and gyro in
        # deg/s (NOT SI). Kept as params so the bench probe can correct them without a
        # code change if a firmware revision reports different units.
        self.declare_parameter('accel_scale', 9.80665 / 1000.0)
        self.declare_parameter('gyro_scale', math.pi / 180.0)
        # Gyro zero-rate bias [x, y, z] in rad/s, subtracted after scaling.
        # Default 0: the firmware already subtracts its own per-axis calibration
        # offsets (TempGyr.*_Off_Err in QMI8658.cpp), and a stationary capture on
        # this unit (2026-07-16) showed the raw stream is unbiased (<0.005 rad/s
        # per axis). The previous vector here had inverted signs and was INJECTING
        # ~0.44/0.17/-0.03 rad/s of phantom rotation. If a future unit does show
        # real bias, measure it as the mean of the published (scaled) rates with
        # this param zeroed, then enter those means directly.
        self.declare_parameter('gyro_bias', [0.0, 0.0, 0.0])
        # Per-axis measurement noise (std dev, SI) -> covariance diagonals, from the
        # same capture. Gyro z is the spikiest axis (occasional non-Gaussian
        # transients); a downstream EKF's outlier rejection absorbs those.
        self.declare_parameter('angular_velocity_stddev', [0.016945, 0.005372, 0.046593])
        self.declare_parameter('linear_acceleration_stddev', [0.02546, 0.02538, 0.03564])
        # Median-of-3 despike on the raw accel/gyro before publishing. The board emits
        # occasional single-sample glitch spikes (~1% of samples, recurring magnitudes);
        # a 3-wide median drops them while preserving real motion (median of a rotation
        # ramp is the middle sample, ~1-sample lag). Set false to publish unfiltered.
        self.declare_parameter('imu_despike', True)

        self._port = self.get_parameter('serial_port').value
        self._baud = int(self.get_parameter('baud').value)
        self._max_linear_speed = float(self.get_parameter('max_linear_speed').value)
        self._max_angular_speed = float(self.get_parameter('max_angular_speed').value)
        self._output_max = float(self.get_parameter('output_max').value)
        self._lin_deadband = float(self.get_parameter('lin_deadband').value)
        self._ang_deadband = float(self.get_parameter('ang_deadband').value)
        self._ang_washout = float(self.get_parameter('ang_deadband_washout').value)
        self._ang_kp = float(self.get_parameter('ang_kp').value)
        self._ang_ki = float(self.get_parameter('ang_ki').value)
        self._ang_integ_max = float(self.get_parameter('ang_integ_max').value)
        self._control_rate = float(self.get_parameter('control_rate').value)
        self._cmd_type = int(self.get_parameter('cmd_type').value)
        self._cmd_timeout = float(self.get_parameter('cmd_timeout').value)

        self._imu_enabled = bool(self.get_parameter('publish_imu').value)
        self._imu_frame_id = str(self.get_parameter('imu_frame_id').value)
        self._imu_poll_rate = float(self.get_parameter('imu_poll_rate').value)
        self._accel_scale = float(self.get_parameter('accel_scale').value)
        self._gyro_scale = float(self.get_parameter('gyro_scale').value)
        self._gyro_bias = [float(v) for v in self.get_parameter('gyro_bias').value]
        av_std = [float(v) for v in self.get_parameter('angular_velocity_stddev').value]
        la_std = [float(v) for v in self.get_parameter('linear_acceleration_stddev').value]
        self._angular_velocity_var = [s * s for s in av_std]
        self._linear_acceleration_var = [s * s for s in la_std]
        self._imu_despike = bool(self.get_parameter('imu_despike').value)

        if self._max_linear_speed <= 0.0 or self._max_angular_speed <= 0.0:
            raise ValueError('max_linear_speed and max_angular_speed must be > 0')
        for _db_name, _db in (('lin_deadband', self._lin_deadband),
                              ('ang_deadband', self._ang_deadband)):
            if not 0.0 <= _db < self._output_max:
                raise ValueError(f'{_db_name} must be in [0, output_max)')
        for _name, _vec in (('gyro_bias', self._gyro_bias),
                            ('angular_velocity_stddev', av_std),
                            ('linear_acceleration_stddev', la_std)):
            if len(_vec) != 3:
                raise ValueError(f'{_name} must have exactly 3 elements, got {len(_vec)}')

        self._serial = serial.Serial(self._port, self._baud, timeout=1.0)
        self.get_logger().info(
            f'Opened {self._port} @ {self._baud} baud '
            f'(max_linear_speed={self._max_linear_speed} m/s, '
            f'max_angular_speed={self._max_angular_speed} rad/s, output_max={self._output_max})'
        )

        self._last_cmd_time = self.get_clock().now()
        self._stopped = True  # avoid spamming stop frames once already stopped

        # Control targets (set by /cmd_vel, consumed by the 20 Hz control tick).
        self._target_lin_c = 0.0   # lifted linear PWM command
        self._target_wz = 0.0      # commanded yaw rate (rad/s)
        self._ang_integ = 0.0      # PI integrator (PWM units)
        # Latest gyro z (rad/s) from the reader thread; float writes are atomic
        # under the GIL, so no lock. Stale samples disable the PI (open-loop
        # fallback).
        self._gz_latest = 0.0
        self._gz_time = 0.0

        self._sub = self.create_subscription(Twist, 'cmd_vel', self._on_cmd_vel, 10)
        # Watchdog runs at a few Hz; it only emits a single stop frame per lapse.
        self._watchdog = self.create_timer(self._cmd_timeout / 2.0, self._on_watchdog)
        # Motor control tick: mixes targets + gyro PI and writes the serial frame.
        # Running it continuously (rather than per-/cmd_vel) also feeds the
        # firmware's 3 s command heartbeat while a command is being held.
        self._control_dt = 1.0 / self._control_rate if self._control_rate > 0.0 else 0.05
        self._control_timer = self.create_timer(self._control_dt, self._on_control_tick)

        # IMU: background serial reader + periodic ``{"T":126}`` poll. Firmware
        # ships with continuous base-info streaming OFF (baseFeedbackFlow = 0), and
        # that stream carries only fused r/p/y -- raw accel/gyro exist solely in the
        # T:126 reply, so the poll rate IS the IMU sample rate. Mirrors the reader in
        # test_utilities/wave_rover_serial_teleop.py.
        self._running = True
        self._reader = None
        if self._imu_enabled:
            self._imu_hist = deque(maxlen=3)  # rolling window for the median despike
            self._imu_pub = self.create_publisher(Imu, 'imu/data_raw', qos_profile_sensor_data)
            self._reader = threading.Thread(target=self._read_loop, daemon=True)
            self._reader.start()
            poll_period = 1.0 / self._imu_poll_rate if self._imu_poll_rate > 0.0 else 0.05
            self._imu_timer = self.create_timer(poll_period, self._on_imu_poll)
            self.get_logger().info(
                f"Publishing IMU on 'imu/data_raw' (frame_id={self._imu_frame_id}, "
                f'poll={self._imu_poll_rate:.1f} Hz)'
            )

    def _lift(self, norm: float, deadband: float) -> float:
        """Dead-zone feedforward for one twist axis: remap a nonzero normalized
        command ([-1, 1]) onto [deadband, output_max] so the smallest command
        still overcomes friction on that axis. A near-zero command stays 0 (a
        true stop) rather than lurching to the dead-zone floor."""
        if abs(norm) < 1e-3:
            return 0.0
        span = self._output_max - deadband
        return math.copysign(deadband + span * abs(norm), norm)

    def _on_cmd_vel(self, msg: Twist) -> None:
        # Record targets only; the control tick does the mixing so the angular
        # PI keeps running between (and independently of) /cmd_vel arrivals.
        # If the previous target's sign disagrees, restart the integrator so a
        # stale trim never pushes the new command the wrong way.
        if msg.angular.z * self._target_wz < 0.0:
            self._ang_integ = 0.0
        lin = _clamp(msg.linear.x / self._max_linear_speed, -1.0, 1.0)
        self._target_lin_c = self._lift(lin, self._lin_deadband)
        self._target_wz = msg.angular.z
        self._last_cmd_time = self.get_clock().now()

    def _ang_feedforward(self, wz: float, lin_c: float) -> float:
        """Open-loop angular PWM for a yaw-rate target: normalize, then lift out
        of the (washed-out while rolling) scrub dead-zone."""
        ang = _clamp(wz / self._max_angular_speed, -1.0, 1.0)
        # Rolling wheels don't fight full static scrub: wash the angular floor
        # out with linear throttle (see ang_deadband_washout).
        if self._ang_washout > 0.0:
            ang_db = self._ang_deadband * max(0.0, 1.0 - abs(lin_c) / self._ang_washout)
        else:
            ang_db = self._ang_deadband
        return self._lift(ang, ang_db)

    def _on_control_tick(self) -> None:
        """20 Hz mixer: linear feedforward + gyro-PI-trimmed angular output."""
        lin_c = self._target_lin_c
        wz = self._target_wz

        if lin_c == 0.0 and wz == 0.0:
            self._ang_integ = 0.0
            if not self._stopped:
                self._send(0.0, 0.0)
            return

        ang_u = self._ang_feedforward(wz, lin_c)
        gyro_fresh = (self._imu_enabled
                      and (time.monotonic() - self._gz_time) < 0.3
                      and (self._ang_kp > 0.0 or self._ang_ki > 0.0))
        if gyro_fresh:
            # PI trim against the measured yaw rate. The friction knee makes the
            # plant nearly a step; the loop dithers across it so the *average*
            # rate tracks the command (and wz=0 actively cancels veer).
            err = wz - self._gz_latest
            self._ang_integ = _clamp(
                self._ang_integ + self._ang_ki * err * self._control_dt,
                -self._ang_integ_max, self._ang_integ_max)
            ang_u += self._ang_kp * err + self._ang_integ

        left = lin_c - ang_u
        right = lin_c + ang_u

        # Preserve the turn ratio if mixing pushed a side past full scale.
        peak = max(self._output_max, abs(left), abs(right))
        self._send(left * self._output_max / peak,
                   right * self._output_max / peak)

    def _on_watchdog(self) -> None:
        elapsed = (self.get_clock().now() - self._last_cmd_time).nanoseconds * 1e-9
        if elapsed >= self._cmd_timeout and not self._stopped:
            self.get_logger().warn(
                f'No /cmd_vel for {elapsed:.2f}s (> {self._cmd_timeout}s); stopping motors.'
            )
            self._target_lin_c = 0.0
            self._target_wz = 0.0
            self._ang_integ = 0.0
            self._send(0.0, 0.0)

    def _write_json(self, obj: dict) -> bool:
        """Serialize ``obj`` as a compact JSON line and write it to the board."""
        try:
            self._serial.write((json.dumps(obj) + '\n').encode('ascii'))
        except serial.SerialException as exc:  # pragma: no cover - hardware fault
            self.get_logger().error(f'Serial write failed: {exc}')
            return False
        return True

    def _send(self, left: float, right: float) -> None:
        # round to keep the serial frames compact; the ESP32 parses standard JSON.
        if self._write_json({'T': self._cmd_type, 'L': round(left, 3), 'R': round(right, 3)}):
            self._stopped = left == 0.0 and right == 0.0

    def _on_imu_poll(self) -> None:
        """Nudge the board for a feedback frame (a no-op if it is already streaming)."""
        self._write_json({'T': 126})

    def _read_loop(self) -> None:
        """Read feedback JSON lines and publish IMU samples until shutdown.

        Mirrors the reader in test_utilities/wave_rover_serial_teleop.py: tolerate
        non-JSON debug lines, and match feedback frames by field presence rather than
        their ``T`` tag (which varies across firmware revisions). Only this thread
        reads the port; only the executor thread writes it, so no lock is needed.
        """
        while self._running:
            try:
                line = self._serial.readline().decode('utf-8', errors='replace').strip()
            except serial.SerialException:
                break
            if not line:
                continue
            try:
                data = json.loads(line)
            except (json.JSONDecodeError, ValueError):
                continue
            if isinstance(data, dict):
                self._publish_imu(data)

    def _publish_imu(self, data: dict) -> None:
        # A usable raw sample needs both accelerometer and gyroscope fields; frames
        # carrying only fused orientation (r/p/y without ax/gx) are skipped.
        if not all(k in data for k in ('ax', 'ay', 'az', 'gx', 'gy', 'gz')):
            return
        try:
            ax, ay, az = float(data['ax']), float(data['ay']), float(data['az'])
            gx, gy, gz = float(data['gx']), float(data['gy']), float(data['gz'])
        except (TypeError, ValueError):
            return

        # Median-of-3 despike: drop single-sample glitch spikes, keep real motion.
        if self._imu_despike:
            self._imu_hist.append((ax, ay, az, gx, gy, gz))
            ax, ay, az, gx, gy, gz = (statistics.median(c) for c in zip(*self._imu_hist))

        msg = Imu()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = self._imu_frame_id
        # Firmware emits accel in milli-g and gyro in deg/s; convert to SI.
        msg.linear_acceleration.x = ax * self._accel_scale
        msg.linear_acceleration.y = ay * self._accel_scale
        msg.linear_acceleration.z = az * self._accel_scale
        # Convert gyro to rad/s, then subtract the measured zero-rate bias.
        msg.angular_velocity.x = gx * self._gyro_scale - self._gyro_bias[0]
        msg.angular_velocity.y = gy * self._gyro_scale - self._gyro_bias[1]
        msg.angular_velocity.z = gz * self._gyro_scale - self._gyro_bias[2]
        # Feed the yaw-rate PI in the control tick (see _on_control_tick).
        self._gz_latest = msg.angular_velocity.z
        self._gz_time = time.monotonic()
        # Raw/unfused: signal "no orientation estimate" per REP-145 so downstream
        # filters ignore the quaternion (kept as a valid identity rotation).
        msg.orientation.w = 1.0
        msg.orientation_covariance[0] = -1.0
        for axis, i in enumerate((0, 4, 8)):
            msg.angular_velocity_covariance[i] = self._angular_velocity_var[axis]
            msg.linear_acceleration_covariance[i] = self._linear_acceleration_var[axis]
        self._imu_pub.publish(msg)

    def stop_and_close(self) -> None:
        """Best-effort stop the motors and close the port (called on shutdown)."""
        self._running = False
        if self._reader is not None:
            self._reader.join(timeout=1.0)
        try:
            if self._serial.is_open:
                self._send(0.0, 0.0)
                self._serial.close()
        except serial.SerialException:
            pass


def main(args=None) -> None:
    rclpy.init(args=args)
    node = WaveRoverDriver()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.stop_and_close()
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()

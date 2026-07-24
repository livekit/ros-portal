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

"""Serial motion driver for the Waveshare Cobra Flex chassis.

BOILERPLATE / UNTESTED ON HARDWARE: the Jetson Orin Nano is not wired into the
chassis yet. Protocol details come from the wiki
(https://www.waveshare.com/wiki/Cobra_Flex) and mirror the WAVE ROVER driver's
serial handling; expect to recheck the serial device path, baud, and feedback
frame fields on first bring-up.

Subscribes to ``cmd_vel`` (``geometry_msgs/Twist``); a control tick converts the
twist to per-side wheel speeds via differential-drive kinematics and sends them
to the ESP32-S3 driver board as JSON over UART, e.g.::

    {"T": 1, "L": 100, "R": 100}

Unlike the WAVE ROVER's open-loop PWM, the Cobra Flex hub motors are closed-loop
(built-in FOC), so ``L``/``R`` are true wheel speeds in 0.1 rpm units
(-1800..1800). No dead-zone lifting or gyro PI trim is needed here.

A watchdog stops the motors if ``cmd_vel`` goes quiet for ``cmd_timeout``
seconds, and the motors are stopped on shutdown.

Sensor feedback: on startup the driver enables the firmware's continuous
feedback stream (``{"T":131,"cmd":1}``, with an optional ``{"T":130}`` poll
fallback). A background reader thread parses the reply frames::

    {"T":1001,"M1":0,"M2":0,"M3":0,"M4":0,"odl":0,"odr":0,"v":1173}

- ``M1..M4``: wheel speeds in 0.1 rpm (M1 front-left, M2 front-right,
  M3 rear-right, M4 rear-left) -> ``sensor_msgs/JointState`` on
  ``wheel_states`` (velocity rad/s; position rad integrated from the per-side
  ``odl``/``odr`` mileage, reported in cm).
- ``v``: battery voltage in 0.01 V -> ``sensor_msgs/BatteryState`` on
  ``battery_state``.

The base chassis has no IMU or other sensors; downstream odometry is derived
from the wheel feedback (see ``cobra_flex_control``).
"""

import json
import threading

from geometry_msgs.msg import Twist
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import BatteryState
from sensor_msgs.msg import JointState

from cobra_flex_driver.kinematics import tenth_rpm_to_rad_s
from cobra_flex_driver.kinematics import twist_to_wheel_tenth_rpm

try:
    import serial
except ImportError as exc:  # pragma: no cover - surfaced clearly at runtime
    raise ImportError(
        'pyserial is required by cobra_flex_driver. Install it with '
        '`rosdep install` (python3-serial) or `pip install pyserial`.'
    ) from exc

# JSON command types from the wiki (defined in the firmware's json_cmd.h).
CMD_SPEED_INPUT = 1          # {"T":1,"L":..,"R":..} per-side speed, 0.1 rpm
CMD_FEEDBACK_POLL = 130      # {"T":130} request one feedback frame
CMD_FEEDBACK_STREAM = 131    # {"T":131,"cmd":0|1} continuous feedback off/on
FEEDBACK_T = 1001            # feedback frame tag (matched by fields, not tag)


class CobraFlexDriver(Node):
    """Bridges ``cmd_vel`` to the Cobra Flex serial JSON protocol."""

    def __init__(self) -> None:
        super().__init__('cobra_flex_driver')

        # Serial link. The ESP32-S3 enumerates over USB (typically /dev/ttyACM0
        # on the Jetson); the dedicated host UART header is an alternative.
        # VERIFY on first hardware bring-up.
        self.declare_parameter('serial_port', '/dev/ttyACM0')
        self.declare_parameter('baud', 115200)
        # Chassis geometry (wiki spec sheet): 74.5 mm drive wheels, 228 mm track.
        self.declare_parameter('wheel_radius', 0.03725)
        self.declare_parameter('track_width', 0.228)
        # Speed limit per wheel, in rpm. Motor no-load speed is 180 rpm (the
        # protocol's +-1800 x 0.1 rpm bound); rated speed is 100 rpm. Commands
        # beyond this are scaled down preserving the turn ratio.
        self.declare_parameter('max_wheel_rpm', 180.0)
        # Control tick: resends the current target periodically so a held
        # command also feeds any firmware command-heartbeat, mirroring the
        # WAVE ROVER driver.
        self.declare_parameter('control_rate', 20.0)
        # Safety / timing.
        self.declare_parameter('cmd_timeout', 0.5)

        # Feedback. continuous_feedback enables the firmware stream (T:131);
        # feedback_poll_rate > 0 additionally polls with T:130 (harmless if the
        # stream is on; the sole source if it is off).
        self.declare_parameter('continuous_feedback', True)
        self.declare_parameter('feedback_poll_rate', 0.0)
        self.declare_parameter('publish_wheel_states', True)
        # Joint names in feedback order M1..M4 (LF, RF, RR, LR).
        self.declare_parameter('wheel_joint_names', [
            'front_left_wheel_joint',
            'front_right_wheel_joint',
            'rear_right_wheel_joint',
            'rear_left_wheel_joint',
        ])
        self.declare_parameter('publish_battery', True)
        # Feedback scaling: "v":1173 -> 11.73 V; "odl"/"odr" are cm.
        self.declare_parameter('voltage_scale', 0.01)
        self.declare_parameter('mileage_scale', 0.01)

        self._port = self.get_parameter('serial_port').value
        self._baud = int(self.get_parameter('baud').value)
        self._wheel_radius = float(self.get_parameter('wheel_radius').value)
        self._track_width = float(self.get_parameter('track_width').value)
        self._max_tenth_rpm = float(self.get_parameter('max_wheel_rpm').value) * 10.0
        self._control_rate = float(self.get_parameter('control_rate').value)
        self._cmd_timeout = float(self.get_parameter('cmd_timeout').value)
        self._continuous_feedback = bool(self.get_parameter('continuous_feedback').value)
        self._poll_rate = float(self.get_parameter('feedback_poll_rate').value)
        self._publish_wheel_states = bool(self.get_parameter('publish_wheel_states').value)
        self._joint_names = [str(n) for n in self.get_parameter('wheel_joint_names').value]
        self._publish_battery = bool(self.get_parameter('publish_battery').value)
        self._voltage_scale = float(self.get_parameter('voltage_scale').value)
        self._mileage_scale = float(self.get_parameter('mileage_scale').value)

        if self._wheel_radius <= 0.0 or self._track_width <= 0.0:
            raise ValueError('wheel_radius and track_width must be > 0')
        if self._max_tenth_rpm <= 0.0:
            raise ValueError('max_wheel_rpm must be > 0')
        if len(self._joint_names) != 4:
            raise ValueError(
                f'wheel_joint_names must have exactly 4 elements, got {len(self._joint_names)}')

        self._serial = serial.Serial(self._port, self._baud, timeout=1.0)
        self.get_logger().info(
            f'Opened {self._port} @ {self._baud} baud '
            f'(wheel_radius={self._wheel_radius} m, track_width={self._track_width} m, '
            f'max_wheel_rpm={self._max_tenth_rpm / 10.0})'
        )

        self._last_cmd_time = self.get_clock().now()
        self._stopped = True  # avoid spamming stop frames once already stopped

        # Control targets (set by cmd_vel, consumed by the control tick).
        self._target_left = 0.0   # 0.1 rpm units
        self._target_right = 0.0

        self._sub = self.create_subscription(Twist, 'cmd_vel', self._on_cmd_vel, 10)
        # Watchdog runs at a few Hz; it only emits a single stop frame per lapse.
        self._watchdog = self.create_timer(self._cmd_timeout / 2.0, self._on_watchdog)
        control_dt = 1.0 / self._control_rate if self._control_rate > 0.0 else 0.05
        self._control_timer = self.create_timer(control_dt, self._on_control_tick)

        # Feedback: background serial reader + firmware stream and/or poll.
        self._running = True
        self._reader = None
        if self._publish_wheel_states:
            self._joint_pub = self.create_publisher(
                JointState, 'wheel_states', qos_profile_sensor_data)
        if self._publish_battery:
            self._battery_pub = self.create_publisher(
                BatteryState, 'battery_state', qos_profile_sensor_data)
        if self._publish_wheel_states or self._publish_battery:
            self._reader = threading.Thread(target=self._read_loop, daemon=True)
            self._reader.start()
            if self._continuous_feedback:
                self._write_json({'T': CMD_FEEDBACK_STREAM, 'cmd': 1})
            if self._poll_rate > 0.0:
                self._poll_timer = self.create_timer(1.0 / self._poll_rate, self._on_poll)
            self.get_logger().info(
                f"Publishing feedback (wheel_states={self._publish_wheel_states}, "
                f'battery={self._publish_battery}, '
                f'continuous={self._continuous_feedback}, poll={self._poll_rate:.1f} Hz)'
            )

    def _on_cmd_vel(self, msg: Twist) -> None:
        self._target_left, self._target_right = twist_to_wheel_tenth_rpm(
            msg.linear.x, msg.angular.z,
            self._wheel_radius, self._track_width, self._max_tenth_rpm)
        self._last_cmd_time = self.get_clock().now()

    def _on_control_tick(self) -> None:
        """Resend the current target so held commands survive firmware heartbeats."""
        if self._target_left == 0.0 and self._target_right == 0.0:
            if not self._stopped:
                self._send(0.0, 0.0)
            return
        self._send(self._target_left, self._target_right)

    def _on_watchdog(self) -> None:
        elapsed = (self.get_clock().now() - self._last_cmd_time).nanoseconds * 1e-9
        if elapsed >= self._cmd_timeout and not self._stopped:
            self.get_logger().warn(
                f'No cmd_vel for {elapsed:.2f}s (> {self._cmd_timeout}s); stopping motors.'
            )
            self._target_left = 0.0
            self._target_right = 0.0
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
        # The firmware takes integer 0.1 rpm values.
        if self._write_json({'T': CMD_SPEED_INPUT, 'L': round(left), 'R': round(right)}):
            self._stopped = left == 0.0 and right == 0.0

    def _on_poll(self) -> None:
        """Nudge the board for a feedback frame (a no-op if it is already streaming)."""
        self._write_json({'T': CMD_FEEDBACK_POLL})

    def _read_loop(self) -> None:
        """Read feedback JSON lines and publish sensor data until shutdown.

        Mirrors the WAVE ROVER reader: tolerate non-JSON debug lines, and match
        feedback frames by field presence rather than their ``T`` tag. Only this
        thread reads the port; only the executor thread writes it, so no lock is
        needed.
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
                self._publish_feedback(data)

    def _publish_feedback(self, data: dict) -> None:
        stamp = self.get_clock().now().to_msg()

        if self._publish_wheel_states and all(
                k in data for k in ('M1', 'M2', 'M3', 'M4')):
            try:
                speeds = [float(data[k]) for k in ('M1', 'M2', 'M3', 'M4')]
            except (TypeError, ValueError):
                return
            msg = JointState()
            msg.header.stamp = stamp
            msg.name = list(self._joint_names)
            msg.velocity = [tenth_rpm_to_rad_s(s) for s in speeds]
            # Wheel angle from the per-side mileage (cm): both wheels of a side
            # share the firmware's odometer, so they get the same position.
            try:
                odl = float(data['odl']) * self._mileage_scale / self._wheel_radius
                odr = float(data['odr']) * self._mileage_scale / self._wheel_radius
                # Feedback order M1..M4 = LF, RF, RR, LR.
                msg.position = [odl, odr, odr, odl]
            except (KeyError, TypeError, ValueError):
                pass  # velocity-only JointState is still useful
            self._joint_pub.publish(msg)

        if self._publish_battery and 'v' in data:
            try:
                voltage = float(data['v']) * self._voltage_scale
            except (TypeError, ValueError):
                return
            msg = BatteryState()
            msg.header.stamp = stamp
            msg.voltage = voltage
            msg.present = True
            msg.power_supply_status = BatteryState.POWER_SUPPLY_STATUS_UNKNOWN
            msg.power_supply_health = BatteryState.POWER_SUPPLY_HEALTH_UNKNOWN
            msg.power_supply_technology = BatteryState.POWER_SUPPLY_TECHNOLOGY_LION
            msg.percentage = float('nan')
            msg.current = float('nan')
            msg.charge = float('nan')
            msg.capacity = float('nan')
            msg.design_capacity = float('nan')
            self._battery_pub.publish(msg)

    def stop_and_close(self) -> None:
        """Best-effort stop the motors and close the port (called on shutdown)."""
        self._running = False
        if self._reader is not None:
            self._reader.join(timeout=1.0)
        try:
            if self._serial.is_open:
                if self._continuous_feedback:
                    self._write_json({'T': CMD_FEEDBACK_STREAM, 'cmd': 0})
                self._send(0.0, 0.0)
                self._serial.close()
        except serial.SerialException:
            pass


def main(args=None) -> None:
    rclpy.init(args=args)
    node = CobraFlexDriver()
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

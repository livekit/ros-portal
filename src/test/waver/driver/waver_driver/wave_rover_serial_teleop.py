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

"""Standalone WASD teleop + IMU readout for the Waveshare WAVE ROVER.

This talks to the ESP32 "General Driver for Robots" board directly over serial
JSON, with **no ROS 2 stack involved** -- use it to confirm the physical link,
motors, and IMU work before bringing up the full launch.

Protocol (ESP32 firmware, 115200 baud):
  * motion:   {"T":1,"L":<-0.5..0.5>,"R":<-0.5..0.5>}   (signed PWM fraction)
  * feedback: the board streams base info as JSON lines tagged "T":1003 that
              include IMU fields (roll/pitch/yaw, accel, gyro). Continuous
              feedback is enabled by default at power-up; we also periodically
              send {"T":126} to request a frame in case it was turned off.

Controls:
  w / s : forward / backward        a / d : rotate left / right
  space : stop                      + / - : increase / decrease speed
  b     : measure gyro zero-rate bias (hold still)   q: quit (stops the motors)

Gyro bias: at rest the gyro reads a small nonzero rate (zero-rate offset). Press
``b`` (or run with ``--measure-bias``) while the robot is stationary to average a
few seconds of samples and print the per-axis bias. The printed rad/s values drop
straight into the ROS ``wave_rover_driver`` ``gyro_bias`` parameter.

Only depends on Python 3 + pyserial (`sudo apt install python3-serial`).
"""

import argparse
import json
import select
import sys
import termios
import threading
import time
import tty

try:
    import serial
except ImportError:
    sys.exit(
        'pyserial not found. Install it with `sudo apt install python3-serial` '
        '(or `pip install pyserial`).'
    )

# IMU / base-feedback fields we know about, in print order, with labels.
# Missing keys are skipped so this tolerates firmware differences.
_IMU_FIELDS = [
    ('r', 'roll'), ('p', 'pitch'), ('y', 'yaw'),
    ('ax', 'ax'), ('ay', 'ay'), ('az', 'az'),
    ('gx', 'gx'), ('gy', 'gy'), ('gz', 'gz'),
    ('v', 'volt'),
]

# The board reports gyro rates in deg/s; the ROS driver publishes rad/s.
_DEG2RAD = 3.141592653589793 / 180.0


class RoverSerial:
    """Serial link to the driver board with a background feedback reader."""

    def __init__(self, port, baud):
        self._serial = serial.Serial(port, baud, timeout=0.5)
        self._latest = {}
        self._raw = ''
        self._lock = threading.Lock()
        self._running = True
        self._collecting = False
        self._samples = []
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self):
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
                with self._lock:
                    self._raw = line
                    # Keep any frame that carries IMU-ish fields.
                    if any(k in data for k in ('r', 'p', 'y', 'ax')):
                        self._latest = data
                    # While calibrating, accumulate raw gyro (deg/s) samples.
                    if self._collecting and all(k in data for k in ('gx', 'gy', 'gz')):
                        try:
                            self._samples.append((float(data['gx']),
                                                  float(data['gy']),
                                                  float(data['gz'])))
                        except (TypeError, ValueError):
                            pass

    def latest_feedback(self):
        with self._lock:
            return dict(self._latest), self._raw

    def send(self, obj):
        try:
            self._serial.write((json.dumps(obj) + '\n').encode('ascii'))
        except serial.SerialException as exc:
            sys.stderr.write(f'\nserial write failed: {exc}\n')

    def set_speed(self, left, right):
        self.send({'T': 1, 'L': round(left, 3), 'R': round(right, 3)})

    def request_feedback(self):
        self.send({'T': 126})

    def measure_bias(self, duration):
        """Average stationary gyro samples for ``duration`` s.

        Returns a list of ``(gx, gy, gz)`` samples in the board's native deg/s.
        The robot must be still -- any motion contaminates the estimate.
        """
        with self._lock:
            self._samples = []
            self._collecting = True
        try:
            end = time.monotonic() + duration
            while time.monotonic() < end:
                self.request_feedback()  # keep frames coming even if not streaming
                time.sleep(0.05)
        finally:
            with self._lock:
                self._collecting = False
                samples = list(self._samples)
        return samples

    def close(self):
        self._running = False
        try:
            self.set_speed(0.0, 0.0)
            self._serial.close()
        except serial.SerialException:
            pass


def _format_imu(feedback):
    parts = []
    for key, label in _IMU_FIELDS:
        if key in feedback:
            try:
                parts.append(f'{label}={float(feedback[key]):7.2f}')
            except (TypeError, ValueError):
                parts.append(f'{label}={feedback[key]}')
    return '  '.join(parts)


def _mean_std(values):
    """Return the population mean and standard deviation of a sequence."""
    n = len(values)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / n
    return mean, var ** 0.5


def _print_bias_report(samples):
    """Print per-axis gyro zero-rate bias + noise from raw deg/s samples."""
    if len(samples) < 2:
        print(f'\nbias: only {len(samples)} gyro sample(s) collected -- is the board '
              'sending feedback? Check the serial link and try again.')
        return
    means, stds = [], []
    for axis in zip(*samples):  # gx[], gy[], gz[]
        mean, std = _mean_std(axis)
        means.append(mean)
        stds.append(std)
    print(f'\ngyro zero-rate bias  (n={len(samples)}, robot must be stationary):')
    print('  axis    bias[deg/s]   noise[deg/s]     bias[rad/s]')
    for label, mean, std in zip('xyz', means, stds):
        print(f'   {label}     {mean:+9.3f}     {std:9.3f}     {mean * _DEG2RAD:+11.6f}')
    rad = [f'{mean * _DEG2RAD:.6f}' for mean in means]
    print('\n  ROS wave_rover_driver parameter (paste into launch/config):')
    print(f'    gyro_bias: [{rad[0]}, {rad[1]}, {rad[2]}]')


def _read_key():
    """Return a pending keypress without blocking, or '' if none."""
    if select.select([sys.stdin], [], [], 0)[0]:
        return sys.stdin.read(1)
    return ''


HELP = (
    'WAVE ROVER serial teleop  |  w/s: fwd/back   a/d: turn L/R   '
    'space: stop   +/-: speed   b: gyro bias   q: quit'
)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', default='/dev/serial0',
                        help='serial device (default: %(default)s)')
    parser.add_argument('--baud', type=int, default=115200,
                        help='baud rate (default: %(default)s)')
    parser.add_argument('--speed', type=float, default=0.3,
                        help='drive throttle fraction 0..0.5 (default: %(default)s)')
    parser.add_argument('--turn', type=float, default=0.3,
                        help='turn throttle fraction 0..0.5 (default: %(default)s)')
    parser.add_argument('--measure-bias', action='store_true',
                        help='measure gyro bias (stationary) and exit; no teleop/TTY needed')
    parser.add_argument('--bias-seconds', type=float, default=5.0,
                        help='seconds to average for a bias measurement (default: %(default)s)')
    args = parser.parse_args()

    rover = RoverSerial(args.port, args.baud)

    # Non-interactive calibration path: measure, print, exit (works over SSH/pipes).
    if args.measure_bias:
        print(f'measuring gyro bias over {args.bias_seconds:.0f}s -- keep the robot still...')
        try:
            _print_bias_report(rover.measure_bias(args.bias_seconds))
        finally:
            rover.close()
        return

    speed = max(0.0, min(0.5, args.speed))
    turn = max(0.0, min(0.5, args.turn))
    left = right = 0.0
    action = 'stop'

    print(HELP)
    print(f'connected to {args.port} @ {args.baud} baud\n')

    old_attrs = termios.tcgetattr(sys.stdin)
    last_send = 0.0
    last_feedback_req = 0.0
    try:
        tty.setcbreak(sys.stdin.fileno())
        while True:
            key = _read_key()
            if key:
                if key == 'q':
                    break
                elif key == 'w':
                    left = right = speed
                    action = 'forward'
                elif key == 's':
                    left = right = -speed
                    action = 'backward'
                elif key == 'a':
                    left, right = -turn, turn
                    action = 'rotate-left'
                elif key == 'd':
                    left, right = turn, -turn
                    action = 'rotate-right'
                elif key == ' ':
                    left = right = 0.0
                    action = 'stop'
                elif key == 'b':
                    # Stop and average a stationary window. cbreak leaves output
                    # post-processing on, so the multi-line report prints cleanly.
                    left = right = 0.0
                    action = 'stop'
                    rover.set_speed(0.0, 0.0)
                    print(f'\n\nmeasuring gyro bias -- hold still for '
                          f'{args.bias_seconds:.0f}s...')
                    _print_bias_report(rover.measure_bias(args.bias_seconds))
                    print()
                    last_send = last_feedback_req = 0.0
                elif key in ('+', '='):
                    speed = min(0.5, round(speed + 0.05, 2))
                    turn = min(0.5, round(turn + 0.05, 2))
                elif key in ('-', '_'):
                    speed = max(0.0, round(speed - 0.05, 2))
                    turn = max(0.0, round(turn - 0.05, 2))

            now = time.monotonic()
            # Resend at ~10 Hz so the board's command watchdog keeps the motors alive.
            if now - last_send >= 0.1:
                rover.set_speed(left, right)
                last_send = now
            # Nudge the board for feedback a couple times a second (no-op if streaming).
            if now - last_feedback_req >= 0.5:
                rover.request_feedback()
                last_feedback_req = now

            feedback, raw = rover.latest_feedback()
            imu = _format_imu(feedback) if feedback else '(no IMU feedback yet)'
            status = f'\r[{action:>12} L={left:+.2f} R={right:+.2f} spd={speed:.2f}] {imu}'
            sys.stdout.write(status.ljust(120))
            sys.stdout.flush()

            time.sleep(0.02)
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_attrs)
        rover.close()
        print('\nstopped, serial closed.')


if __name__ == '__main__':
    main()

# WAVE ROVER Physical Stack

ROS 2 stack for the [Waveshare WAVE ROVER](https://www.waveshare.com/wiki/WAVE_ROVER)
4WD rover, driven from a Raspberry Pi over serial.

## Packages

| Directory | Package | Purpose |
| --- | --- | --- |
| `driver/` | `waver_driver` | Serial JSON bridge to the ESP32 board: `cmd_vel` in; PWM motor commands out; `imu/data_raw` out. |
| `bringup/` | `waver_bringup` | Top-level physical/sim launch and LiveKit bridge config. |
| `localization/` | `waver_localization` | EKF launch/config and rf2o covariance relay. |
| `navigation/` | `waver_navigation` | SLAM Toolbox, Nav2, and launch/config. |
| `simulation/` | `waver_simulation` | Gazebo launch and sim frame-prefix relay. |

## Hardware Summary

- ESP32 "General Driver for Robots" board on the Raspberry Pi UART.
- Four DC gear motors with no wheel encoders. Motor commands are signed PWM
  fractions, not true wheel velocities.
- Onboard IMU is polled by the driver and published as `sensor_msgs/Imu` on
  `imu/data_raw`.
- RPLidar C1 provides `/scan`; rf2o laser odometry provides motion feedback
  because the base has no wheel odometry.

## Physical Robot Bringup

The ESP32 board is on the Raspberry Pi 40-pin UART. On this board that is the
mini-UART `/dev/ttyS0`; `/dev/serial0` may exist on the host, but is often absent
inside the dev container. Enable the serial port with `raspi-config` and make sure
the user can access it with `sudo usermod -aG dialout $USER`, then re-login.

Bench-test the serial link, motors, and IMU before starting the full ROS graph:

```bash
python3 src/test/waver/driver/waver_driver/wave_rover_serial_teleop.py --port /dev/ttyS0

# after colcon build:
ros2 run waver_driver wave_rover_serial_teleop.py --port /dev/ttyS0
```

Launch the physical stack:

```bash
ros2 launch waver_bringup waver.launch.xml \
  rover_serial_port:=/dev/ttyS0 \
  lidar_serial_port:=/dev/ttyUSB0
```

Drive it from a second terminal:

```bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

## Key Arguments

- `sim` (`false`): run against Gazebo instead of hardware.
- `sim_gui` (`false`): launch the Gazebo GUI client when sim is enabled.
- `nav2` (`true`): launch the full Nav2 navigation stack.
- `use_camera` (`false`): launch the IMX219 CSI camera package when available.
- `robot_name` (`robot_1`): TF frame prefix and LiveKit identity alignment.
- `rover_serial_port` (`/dev/serial0`), `rover_baud` (`115200`): ESP32 board UART.
- `lidar_serial_port` (`/dev/ttyUSB0`): RPLidar C1 device.
- `max_linear_speed`, `max_angular_speed`: calibration gains used by the open-loop
  PWM mixer.
- `lidar_x`, `lidar_z`, `lidar_yaw`: physical lidar pose relative to `base_link`.

## Middleware

This stack runs on Cyclone DDS. The dev container installs
`ros-$ROS_DISTRO-rmw-cyclonedds-cpp` and sets:

```dockerfile
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ENV ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
```

Discovery is intentionally restricted to localhost because each robot's local DDS
graph runs on one Pi; cross-machine transport is handled by LiveKit.

## Notes

- `waver_driver` sends `{"T":1,"L":l,"R":r}` frames where `l,r` are signed PWM
  fractions. The base WAVE ROVER has no wheel encoders.
- The vendored `waver` URDF's `lidar_link` models the simulator's LD19 and is left
  as an unused visual frame on the real robot; the physical scan is published in
  `laser`.
- Foxglove may render the robot model pitched 90 degrees until the 3D panel mesh
  up axis is set to `Z-up`; the URDF and TF tree are already Z-up.

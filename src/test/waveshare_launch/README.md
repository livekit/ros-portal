# waveshare_launch

Launch package for bringing up the Waveshare WAVE ROVER stack from one entrypoint,
on the **physical robot** or in **Gazebo**.

`waveshare.launch.xml` (default `sim:=false`, physical robot) starts:
- `waver_description` — `robot_state_publisher` + `joint_state_publisher` (`sim_control:=ros2`)
- `rplidar_ros` — RPLidar C1 driver publishing `/scan` in frame `laser`
- a **static TF** `base_link -> laser` for the physical lidar mount
- `waveshare_driver` — bridges `/cmd_vel` to the ESP32 board over serial JSON
- `rf2o_laser_odometry` — laser scan-matching odometry (`/odom` + `odom -> base_footprint`),
  since the base WAVE ROVER has no wheel encoders
- `waver_nav` online async SLAM (`slam_toolbox`, mapping mode)

With `sim:=true` the hardware nodes above are skipped and Gazebo supplies
`/scan`, `/odom`, and `/cmd_vel` instead (via `gazebo.launch.xml`).

## Physical robot bring-up

The rover's ESP32 "General Driver for Robots" board is on the Raspberry Pi 40-pin
UART, which is the **mini-UART `/dev/ttyS0`** on this board. On the host that device
is also reachable via the `/dev/serial0` alias, but inside the dev container that
udev symlink is not created — use `/dev/ttyS0` directly. Enable the serial port
(`raspi-config` > Interface > Serial: login shell off, hardware serial on) and make
sure your user can access it (`sudo usermod -aG dialout $USER`, then re-login).

### Bench test without ROS 2 (recommended first step)

Before bringing up the full stack, confirm the serial link, motors, and IMU work
with the standalone script in `test_utilities` (pure Python + `python3-serial`, no
ROS graph):

```bash
python3 src/test/test_utilities/scripts/wave_rover_serial_teleop.py --port /dev/ttyS0
# after `colcon build` you can also run it as:
#   ros2 run test_utilities wave_rover_serial_teleop.py --port /dev/ttyS0
```

Drive with `w`/`s` (forward/back), `a`/`d` (rotate left/right), `space` to stop,
`+`/`-` to change speed, `q` to quit. Live IMU data (roll/pitch/yaw, accel, gyro,
battery voltage) streams on the status line. If wheels spin the right way and IMU
values update, the hardware link is good.

Then launch the stack (adjust ports/speed to your setup):

```bash
ros2 launch waveshare_launch waveshare.launch.xml \
  rover_serial_port:=/dev/ttyS0 \
  lidar_serial_port:=/dev/ttyUSB0
```

Drive it from a second terminal (keyboard teleop is not auto-launched because it
needs its own TTY):

```bash
sudo apt-get install ros-$ROS_DISTRO-teleop-twist-keyboard   # if not already installed
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

### Key arguments
- `sim` (`false`): run against Gazebo instead of hardware.
- `sim_gui` (`false`): launch the Gazebo GUI client (sim only).
- `foxglove` (`false`): launch the Foxglove bridge with default settings.
- `nav2` (`false`): also launch the full nav2 navigation stack.
- `rover_serial_port` (`/dev/serial0`), `rover_baud` (`115200`): ESP32 board UART.
  Inside the dev container pass `rover_serial_port:=/dev/ttyS0` (the `serial0` alias
  is not created there).
- `lidar_serial_port` (`/dev/ttyUSB0`): RPLidar C1 device.
- `max_linear_speed` (`1.0`): linear speed in m/s mapped to full throttle. Tune so a
  commanded `/cmd_vel` linear.x roughly matches the observed speed.
- `max_angular_speed` (`1.5`): angular speed in rad/s mapped to full throttle. Lower
  it for stronger in-place turns (the driver scales linear/angular independently, so
  rotation isn't crushed by the small track width).
- `lidar_x` (`-0.04`), `lidar_z` (`0.205`), `lidar_yaw` (`3.14159`): lidar pose relative
  to `base_link`. The lidar (and Pi) are mounted rotated 180° about Z from the robot's
  true forward, so `lidar_yaw` defaults to π — this keeps `/scan`, the map, and rf2o
  odometry aligned with true forward. Set it back to `0.0` if you remount the lidar
  facing forward.

### Notes
- `waveshare_driver` sends `{"T":1,"L":l,"R":r}` frames where `l,r ∈ [-0.5, 0.5]` are
  signed PWM fractions (base WAVE ROVER firmware). For older firmware that expects
  `±255` PWM values, override the driver's `output_max` parameter to `255`.
- The vendored `waver` URDF's `lidar_link` models the simulator's LD19 and is left as
  an unused visual frame; the real scan is published in `laser`.
- **Foxglove**: the robot model appears pitched 90° (bottom facing forward) until you set
  the 3D panel's **Scene > Mesh up axis** to **`Z-up`**. Foxglove defaults STL/OBJ meshes
  to Y-up; the URDF and TF are correct (Z-up), so this is a viewer setting only and does
  not affect mapping. RViz2 renders it upright without any change.

To launch only the Gazebo simulation entrypoint, run:

```bash
ros2 launch waveshare_launch gazebo.launch.xml
```

## Middleware (DDS / RMW)

This stack runs on **Cyclone DDS**, not the ROS 2 default (Fast DDS). On the
CPU-constrained Pi 4, Fast DDS produced continuous TF extrapolation errors
(`Lookup would require extrapolation into the past/future`, message-filter drops)
because its heavier thread/discovery overhead starved the `slam_toolbox`
(`map -> odom`) and `rf2o` (`odom -> base_footprint`) transform publishers.
Switching to Cyclone DDS — lighter and the middleware Nav2 is most reliable with —
eliminated those errors.

The dev container (root `Dockerfile`) installs `ros-$ROS_DISTRO-rmw-cyclonedds-cpp`
and bakes both settings in as `ENV`, so **every shell and process in the container
inherits them automatically** — no manual `export` needed:

```dockerfile
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ENV ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
```

**Discovery scope.** Every DDS participant here lives on the one Pi — cross-machine
transport is handled by the LiveKit bridge, not raw DDS — so discovery is restricted
to localhost. This saves CPU (no subnet-wide discovery multicast) and prevents two
robots on the same LAN + `ROS_DOMAIN_ID` from cross-discovering each other's graphs.

Trade-off: with `LOCALHOST` you can no longer reach the graph with `ros2` CLI or
RViz from another machine directly — inspect from on the Pi, or via LiveKit/Foxglove.
To temporarily allow remote DDS access, override at container run time with
`-e ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`.

## LiveKit bridge

`waveshare_livekit_local.launch.py` is a top-level entrypoint that brings up the
Waveshare stack together with the LiveKit bridge (from `ros2_livekit_bridge`).
Keeping this composition here means `ros2_livekit_bridge` stays independent of
the robot-specific `waveshare_launch` package.

__NOTE__: This is intended to be used when a local SFU is running and you dont have credentials.
When a cloud SFU is being used, it is recommended you launch the bridge directly using the launch file in the ros2_livekit_bridge package.  

It forwards `sim`, `sim_gui`, and `foxglove` to `waveshare.launch.xml`, and
`config`, `livekit_url`, `identity`, `token_valid_for`, `use_dev_credentials`,
and `ns` to `livekit_bridge_local.launch.py`. When `sim:=true`, the bridge start
is delayed briefly so the sim stack can come up first.

```bash
ros2 launch waveshare_launch waveshare_livekit_local.launch.py \
  sim:=true \
  config:=$(ros2 pkg prefix --share waveshare_launch)/config/waveshare_livekit_robot.yaml \
  identity:=robot
```

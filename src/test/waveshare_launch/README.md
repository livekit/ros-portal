# waveshare_launch

Launch package for bringing up the Waveshare/Waver robot stack from one entrypoint.

`waveshare.launch.xml` starts:
- `waver_description`
- `nav2_bringup`
- `waver_nav` online async SLAM

Optional flags:
- `sim:=true` also launches `gazebo.launch.xml`
- `sim_gui:=true` launches the Gazebo GUI client when display forwarding is available
- `foxglove:=true` launches the Foxglove bridge with its default settings

For keyboard teleop:
1. Install the teleop_twist_keyboard package:
```bash
sudo apt-get install ros-$ROS_DISTRO-teleop-twist-keyboard
```

```bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

To launch only the Gazebo simulation entrypoint, run:

```bash
ros2 launch waveshare_launch gazebo.launch.xml
```

## LiveKit bridge

`waveshare_livekit_local.launch.py` is a top-level entrypoint that brings up the
Waveshare stack together with the LiveKit bridge (from `ros2_livekit_bridge`).
Keeping this composition here means `ros2_livekit_bridge` stays independent of
the robot-specific `waveshare_launch` package.

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

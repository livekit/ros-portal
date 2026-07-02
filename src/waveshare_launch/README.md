# waveshare_launch

Launch package for bringing up the Waveshare/Waver robot stack from one entrypoint.

`waveshare.launch.xml` starts:
- `waver_description`
- `nav2_bringup`
- `waver_nav` online async SLAM

Optional flags:
- `sim:=true` also launches `gazebo.launch.xml`
- `rviz:=true` also launches `waver_viz`
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

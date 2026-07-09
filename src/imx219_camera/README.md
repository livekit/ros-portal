# imx219_camera

Bringup for the **Arducam 8MP IMX219 (175° wide-angle, CSI)** camera on a
Raspberry Pi, publishing frames onto the ROS 2 graph as
[`sensor_msgs/msg/Image`](https://docs.ros2.org/latest/api/sensor_msgs/msg/Image.html).

It is a thin wrapper around the open-source
[`camera_ros`](https://github.com/christianrauch/camera_ros) node
(`ros-jazzy-camera-ros`), which drives the camera through **libcamera**.
libcamera owns the Raspberry Pi ISP (debayer / auto-exposure / auto-white-
balance), so the published image is already a usable `bgr8` frame rather than
raw Bayer. This package adds only what is needed to make that node run reliably
inside this container.

## Topics

| Topic | Type | Notes |
| --- | --- | --- |
| `/camera/image_raw` | `sensor_msgs/msg/Image` | `bgr8`, default 1280×720 |
| `/camera/image_raw/compressed` | `sensor_msgs/msg/CompressedImage` | JPEG |
| `/camera/camera_info` | `sensor_msgs/msg/CameraInfo` | uncalibrated until you set `camera_info_url` |

## Quick start

```bash
# 1) One-time per container boot: start udev so libcamera can enumerate the camera.
sudo ros2 run imx219_camera setup_camera_udev.sh

# 2) Launch the camera.
ros2 launch imx219_camera imx219_camera.launch.py

# 3) Verify.
ros2 topic hz /camera/image_raw
ros2 topic echo /camera/image_raw --no-arr --once
```

Launch arguments (all optional): `camera`, `format`, `width`, `height`,
`frame_id`, `namespace`, `camera_info_url`. Example:

```bash
ros2 launch imx219_camera imx219_camera.launch.py width:=1920 height:=1080 frame_id:=camera_optical
```

Advanced libcamera controls (frame rate, AE/AWB, orientation, …) live in
[`config/imx219_camera.yaml`](config/imx219_camera.yaml).

## Why the two extra pieces

Both are consequences of running libcamera in a container rather than of the
camera itself. On a stock Raspberry Pi OS host neither is needed.

1. **`setup_camera_udev.sh`** — libcamera maps media entities to `/dev/video*`
   nodes via udev. This container starts without a udev daemon, so libcamera
   reports *"no cameras available"* despite a healthy media graph. The script
   starts `systemd-udevd` and triggers device enumeration.

2. **`libcamera_ipa_inprocess.so`** (LD_PRELOAD'ed by the launch file) — the
   prebuilt `ros-jazzy-libcamera` has its IPA modules signed at build time, but
   packaging edits the shared objects (rpath) *after* signing, invalidating the
   signature. libcamera then runs the IPA in an isolated worker process, which
   crashes here at start (`ControlSerializer::deserialize<ControlList>` →
   `Failed to call start: -110`). The interposer forces the (genuine, unmodified)
   IPA to load **in-process**, which is how a normal Raspberry Pi build runs it.
   See [`src/libcamera_ipa_inprocess.cpp`](src/libcamera_ipa_inprocess.cpp).

To make udev automatic, add the setup script to the devcontainer
`postStartCommand` (or any container entrypoint).

## Calibration

`camera_info` is published but empty until calibrated. To calibrate, run
`camera_calibration` against `/camera/image_raw`, save the result, and pass it:

```bash
ros2 launch imx219_camera imx219_camera.launch.py \
  camera_info_url:=file:///root/.ros/camera_info/imx219.yaml
```

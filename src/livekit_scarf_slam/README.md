# livekit_scarf_slam

This package runs a
[ScaRF-SLAM](https://github.com/ori-drs/ScaRF-SLAM) demo through `ros_portal`.
The robot (a Jetson) sends one stereo camera stream through LiveKit. A SLAM
host (a MacBook Pro for now) does the localization and mapping.

The robot has no IMU, so the demo does not use OpenVINS, the default
ScaRF-SLAM pose source. RTAB-Map stereo SLAM (stereo odometry, loop closure,
and pose graph optimization) gives the poses instead.

The data flows in this order:

1. The camera driver on the robot publishes one side-by-side (SBS) JPEG frame
   on `/stereo/image_raw/compressed`. An SBS frame holds the left and right
   images next to each other, so both eyes have one stamp.
2. `ros_portal` sends the topic as the LiveKit video track `/stereo/image_raw`.
3. `ros_portal` on the SLAM host publishes the track on
   `/stereo/image_raw/compressed`.
4. `stereo_rectify_node` splits each frame and rectifies both fisheye images
   for RTAB-Map. Rectified images have matching points on the same row.
5. RTAB-Map calculates the poses.
6. `slam_bag_recorder_node` writes the ScaRF-SLAM input bag. For each RTAB-Map
   keyframe, the bag has the left fisheye image, the odometry pose, and the
   loop-corrected trajectory.
7. After the run, ScaRF-SLAM builds the dense map from the bag.

ScaRF-SLAM always reads a recorded bag, also in its upstream online mode.
RTAB-Map runs live on the SLAM host. The dense map comes after the run.

## Python Environment

The upstream page uses a conda environment with Python 3.11. Do not use conda
in the devcontainer. ROS 2 Jazzy, `rclpy`, and colcon use the system Python
3.12 at `/usr/bin/python3`, and a different Python version breaks them.

Instead, you create a venv (an isolated Python package folder) from the system
Python with `--system-site-packages`. With this flag, the venv sees the ROS
Python packages and colcon. The ScaRF-SLAM packages go into the venv only.

Do not use `/livekit_ws/venv`. That folder is a macOS venv from the host
mount, and it does not run in the container.

The devcontainer ships `numpy` 1.26.4. Depth Anything 3 requires `numpy<2`, and
the Jazzy apt packages, for example `cv_bridge`, use numpy 1.x. Do not upgrade
numpy to 2.x in the venv.

## Prerequisites

You run all commands in this page inside the devcontainer, as `root`.

1. Start the devcontainer from the repository root on the host:

   ```bash
   devcontainer up --workspace-folder .
   ```

2. Open a shell in the container:

   ```bash
   devcontainer exec --workspace-folder . bash
   ```

3. Install the apt packages:

   ```bash
   apt update
   apt install -y python3-venv ros-$ROS_DISTRO-rtabmap-ros
   ```

   The development image installs `ros-$ROS_DISTRO-rtabmap-ros`. If you use an
   older container, install it here.

The venv in `/opt` and the clones in `~/git` are outside the host mount. If
you recreate the container, do all the steps again.

## Create The Venv

1. Create the venv from the system Python:

   ```bash
   python3 -m venv --system-site-packages /opt/scarf_venv
   ```

2. Add a `COLCON_IGNORE` file, so that colcon does not scan the venv:

   ```bash
   touch /opt/scarf_venv/COLCON_IGNORE
   ```

3. Source ROS 2, then activate the venv:

   ```bash
   source /opt/ros/jazzy/setup.bash
   source /opt/scarf_venv/bin/activate
   ```

4. Make sure that the venv uses Python 3.12 and sees `rclpy`:

   ```bash
   python -c "import sys, rclpy, numpy; print(sys.version, numpy.__version__)"
   ```

Source ROS 2 before you activate the venv in every new shell. If you do it in
the opposite order, the ROS setup script can put the system Python first.

## Install The Python Dependencies

Do these steps in a shell with the venv active.

1. Clone ScaRF-SLAM and Depth Anything 3:

   ```bash
   mkdir -p ~/git && cd ~/git
   git clone https://github.com/ori-drs/ScaRF-SLAM.git
   git clone https://github.com/ByteDance-Seed/Depth-Anything-3.git
   ```

2. Create a pip constraints file (a list of version limits for every
   `pip install`):

   ```bash
   printf 'numpy<2\nsetuptools>=77.0.3,<80\n' > /opt/scarf_venv/constraints.txt
   ```

   Depth Anything 3 and the Jazzy apt packages need `numpy<2`. torch needs
   `setuptools>=77.0.3`, and `colcon-core` needs `setuptools<80`. If
   setuptools is 80 or newer, `colcon build --symlink-install` can fail for
   Python packages. Pass `-c /opt/scarf_venv/constraints.txt` to every
   `pip install` in this section.

3. Install PyTorch and xformers:

   ```bash
   pip install -c /opt/scarf_venv/constraints.txt "torch>=2" torchvision xformers
   ```

4. On an `x86_64` host, install Depth Anything 3 in editable mode:

   ```bash
   cd ~/git/Depth-Anything-3
   pip install -c /opt/scarf_venv/constraints.txt -e . addict
   ```

   Depth Anything 3 imports `addict` but does not list it as a dependency, so
   you install it in the same command.

5. On an `aarch64` host, do the steps in
   [Depth Anything 3 On aarch64](#depth-anything-3-on-aarch64) instead of
   step 4.

6. Install the other ScaRF-SLAM dependencies:

   ```bash
   pip install -c /opt/scarf_venv/constraints.txt rosbags open3d gtsam vismatch
   ```

7. Make sure that the packages import, that numpy is still 1.x, and that
   setuptools is still below 80:

   ```bash
   python -c "import torch, depth_anything_3.api, open3d, gtsam, rosbags, vismatch, numpy, setuptools; print(torch.__version__, numpy.__version__, setuptools.__version__)"
   ```

   The warning about `gsplat` is normal. ScaRF-SLAM does not need 3D Gaussian
   splatting rendering.

The devcontainer on an Apple silicon host is `aarch64` and has no NVIDIA GPU.
On that host, PyTorch installs the CUDA build, but it runs on the CPU only.
xformers has no `aarch64` wheel, so pip compiles it without the CUDA kernels.
For the real-time demo, use an `x86_64` host with an NVIDIA GPU.

Optional: to add sky masks to the Depth Anything 3 output, edit `da3.py`. In
the `forward` function of `NestedDepthAnything3Net`, add this line just before
the `return` statement:

```python
output.sky = metric_output.sky
```

The upstream page also points to
[pull request #7](https://github.com/ori-drs/ScaRF-SLAM/pull/7). It is a
faster implementation for large environments, but nobody reviewed it
independently.

### Depth Anything 3 On aarch64

`pycolmap` has no Linux `aarch64` wheel on PyPI, so `pip install -e .` fails
with this error:

```text
ERROR: No matching distribution found for pycolmap
```

Depth Anything 3 uses `pycolmap` only for COLMAP export, but
`depth_anything_3.api` imports it at load time. You install the other
dependencies, and then you make that import optional.

1. Install the Depth Anything 3 dependencies without `pycolmap`:

   ```bash
   cd ~/git/Depth-Anything-3
   pip install -c /opt/scarf_venv/constraints.txt pre-commit trimesh einops \
     huggingface_hub imageio opencv-python open3d fastapi uvicorn requests \
     typer pillow omegaconf evo e3nn "moviepy==1.0.3" plyfile pillow_heif \
     safetensors addict
   ```

2. Install Depth Anything 3 without its dependency list:

   ```bash
   pip install -c /opt/scarf_venv/constraints.txt --no-deps -e .
   ```

3. In `src/depth_anything_3/utils/export/colmap.py`, replace
   `import pycolmap` with this block:

   ```python
   try:
       import pycolmap
   except ImportError:  # no aarch64 wheel; only export_to_colmap needs it
       pycolmap = None
   ```

4. In the same file, add this check in `export_to_colmap`, just before the
   `# 2. Set Reconstruction` comment:

   ```python
   if pycolmap is None:
       raise ImportError("pycolmap is required for COLMAP export")
   ```

After these steps, `pip check` reports
`depth-anything-3 0.0.0 requires pycolmap, which is not installed`. This
message is expected on `aarch64`. COLMAP export does not work on this host.

## Build The Workspace

ScaRF-SLAM and Depth Anything 3 are plain Python projects, so colcon does not
build them. colcon builds this package and `ros_portal`.

1. Build the demo packages:

   ```bash
   cd /livekit_ws
   colcon build --symlink-install --packages-up-to livekit_scarf_slam
   ```

   `--packages-up-to` builds only this package and its dependencies. Do
   not build the full workspace. The full build also builds the
   `client-sdk-cpp` checkout as the `livekit` package, and that build needs
   Rust. `ros_portal` downloads a prebuilt LiveKit SDK, so it does not need
   that package. Without Rust, the full build fails with this error:

   ```text
   Could not find CARGO_EXECUTABLE using the following names: cargo
   ```

   If the build fails with
   `c++: fatal error: Killed signal terminated program cc1plus`, the
   container ran out of memory. Build one package at a time with two compile
   jobs:

   ```bash
   MAKEFLAGS="-j2" colcon build --symlink-install \
     --executor sequential \
     --packages-up-to livekit_scarf_slam
   ```

2. Source the workspace:

   ```bash
   source install/setup.bash
   ```

3. Run the tests:

   ```bash
   colcon test --packages-select livekit_scarf_slam
   colcon test-result --verbose
   ```

If you built the workspace before without `--symlink-install`, the build can
fail with this error:

```text
failed to create symbolic link '/livekit_ws/build/ros_portal_msgs/ament_cmake_python/ros_portal_msgs/ros_portal_msgs' because existing path cannot be removed: Is a directory
```

The old build copied the Python package as a folder, and colcon cannot
replace that folder with a symlink. Delete the build and install output of
the package in the error, then build again:

```bash
rm -rf build/ros_portal_msgs install/ros_portal_msgs
colcon build --symlink-install --packages-up-to livekit_scarf_slam
```

## Configuration

Before the first run, calibrate the stereo camera. See
[calibration.md](calibration.md). The launch file reads the calibration from
`/livekit_ws/stereo-calibration/stereo_calibration.json` by default.

| File | Purpose |
| --- | --- |
| `config/ros_portal/edge_robot.yaml` | ROS Portal on the robot. Sends `/stereo/image_raw/compressed` as a video track at 8000 kbps. |
| `config/ros_portal/slam_host.yaml` | ROS Portal on the SLAM host. Receives the `/stereo/image_raw` track. |
| `config/rtabmap/stereo.yaml` | RTAB-Map parameters. `frame_id` is `stereo_link`. RTAB-Map adds at most 3 keyframes per second. |
| `config/scarf_slam/cobra_stereo.yaml` | ScaRF-SLAM configuration. The camera blocks come from the calibration. |

The camera driver on the robot must use the calibrated mode, 3840x1080. A
frame rate of 15 fps is enough for RTAB-Map. If LiveKit lowers the stream
resolution, `stereo_rectify_node` scales the intrinsics, and the rectified
images keep the same size. A frame with a different aspect ratio is dropped.

`stereo_slam.launch.py` has these arguments:

| Argument | Default | Description |
| --- | --- | --- |
| `calibration_path` | `/livekit_ws/stereo-calibration/stereo_calibration.json` | The stereo calibration. |
| `sbs_topic` | `/stereo/image_raw/compressed` | The SBS JPEG topic. |
| `output_dir` | `~/scarf_slam_runs/<date>_<time>` | The run folder. It gets `slam_bag` and `rtabmap.db`. |
| `decode_reduction` | `2` | Decode the JPEG at 1/2 size. Use 1, 2, 4, or 8. |
| `rect_scale` | `0.5` | Size of the rectified images. 0.5 gives 960x540. |
| `use_ros_portal` | `true` | Set to `false` to play a recorded SBS bag instead. |
| `rtabmap_viz` | `false` | Start the RTAB-Map GUI. The GUI needs a display. |

The static transforms are `stereo_link` to `cam0_optical` (the left lens)
to `cam0_rect_optical` (the rectified left image). `stereo_link` has x
forward, y left, and z up. To connect the camera to the rover body, publish a
transform from your robot base frame to `stereo_link`.

## Run

1. On the robot, start the camera driver, so that it publishes
   `/stereo/image_raw/compressed`.

2. On the robot, start ROS Portal:

   ```bash
   ros2 launch livekit_scarf_slam edge_robot.launch.py \
     livekit_url:=<url> room_name:=<room>
   ```

3. On the SLAM host, in the devcontainer, start the SLAM host:

   ```bash
   ros2 launch livekit_scarf_slam stereo_slam.launch.py \
     livekit_url:=<url> room_name:=<room>
   ```

4. Drive the rover. Keep the first two seconds slow, so that the odometry
   can start.

5. Stop the launch with `Ctrl+C`. The recorder writes the final trajectory
   and closes the bag. The log line `Closed <path> with <n> keyframes`
   shows the bag path.

6. Build the dense map from the bag, in a shell with the venv active:

   ```bash
   cd ~/git/ScaRF-SLAM
   python3 run_mapping.py --slam_folder <output_dir> \
     --input_bag <output_dir>/slam_bag \
     --config /livekit_ws/src/livekit_scarf_slam/config/scarf_slam/cobra_stereo.yaml
   ```

On the Mac, Depth Anything 3 runs on the CPU, so this step is slow. For
the real-time demo, use an `x86_64` host with an NVIDIA GPU.

To test the SLAM host without the robot, record the SBS topic once with
`ros2 bag record /stereo/image_raw/compressed`. Then start the launch with
`use_ros_portal:=false` and play the bag.

If RTAB-Map logs `quality=0` for many frames, the odometry lost track. Drive
slower, add light, or point the camera at surfaces with more texture.

For more ScaRF-SLAM detail, see the upstream wiki:

- [Online Reconstruction with SLAM](https://github.com/ori-drs/ScaRF-SLAM/wiki/4.-%F0%9F%9A%80-Online-Reconstruction-with-SLAM)
- [Configuration](https://github.com/ori-drs/ScaRF-SLAM/wiki/6.-%E2%9A%99%EF%B8%8F-Configuration)

In every new shell, source the environments in this order:

```bash
source /opt/ros/jazzy/setup.bash
source /opt/scarf_venv/bin/activate
source /livekit_ws/install/setup.bash
```

## Add An IMU Later

With an IMU on the robot, OpenVINS can give the poses instead of RTAB-Map.
OpenVINS uses the IMU for scale and fast motion. Then you need these steps:

1. Calibrate the camera to the IMU. See the IMU part of
   [calibration.md](calibration.md).
2. Clone `open_vins` and `ov_secondary_scarf` into `src/externals`. On
   Jazzy, use the branch of
   [open_vins pull request #543](https://github.com/rpng/open_vins/pull/543).
3. Add `ov_msckf` and `ov_secondary_loop_fusion` to `package.xml`.

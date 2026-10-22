# Camera Calibration

This page tells you how to calibrate the stereo fisheye camera for the
ScaRF-SLAM demo. The SLAM host reads the camera model from a calibration
file. It does not find the model on its own.

The stereo demo needs only the stereo calibration. The IMU calibration is
for a later version of the robot with an IMU.

## Stereo Calibration

You calibrate the stereo camera with the ChArUco scripts in the
`stereo_cal` repository. A ChArUco board is a checkerboard with a coded
marker in each white square. The scripts show the board on a MacBook Pro
screen, so you do not need a printer.

The calibration gives these values:

- The intrinsics of each lens: focal length, image center, and lens
  distortion.
- The rotation and translation of the right lens relative to the left lens.
- The stereo rectification of both lenses.

Do these steps on the Mac:

1. Set the camera to the mode that the robot streams, 3840x1080. The
   intrinsics are valid for one resolution only.

2. Follow the `stereo_cal` README to show the board, capture the images, and
   calculate the calibration.

3. Make sure that the baseline in the summary matches the distance between
   the lens centers, measured with a ruler.

4. Copy `stereo_calibration.json` to `stereo-calibration/` in this
   repository. The devcontainer sees it as
   `/livekit_ws/stereo-calibration/stereo_calibration.json`.

5. In the devcontainer, update the camera blocks of the ScaRF-SLAM
   configuration:

   ```bash
   ros2 run livekit_scarf_slam convert_stereo_calibration.py scarf \
     stereo-calibration/stereo_calibration.json \
     src/livekit_scarf_slam/config/scarf_slam/cobra_stereo.yaml
   ```

At start, `stereo_rectify_node` and `slam_bag_recorder_node` read the JSON
file. ScaRF-SLAM reads only `cobra_stereo.yaml`, so do step 5 after
each new calibration.

In `cobra_stereo.yaml`, `T_cam_imu` of `fisheye_cam0` is the identity, and
`T_cam_imu` of `fisheye_cam1` is the transform from the left lens to the
right lens. ScaRF-SLAM uses each pose in the bag as the left lens pose, and
it reads `T_cam_imu` only to find the offset between the two lenses.

If you mount the camera again or change its focus, calibrate again.

## Add An IMU Later

With an IMU, OpenVINS can give the poses instead of RTAB-Map. OpenVINS reads
the camera and IMU model from its configuration files. The rest of this page
tells you how to calibrate the camera to the IMU.

You get these values from the IMU calibration:

- The extrinsics of each camera: its position and rotation relative to the
  IMU.
- The time offset between the camera clock and the IMU clock.
- The IMU noise values.

OpenVINS refines the intrinsics, the extrinsics, and the time offset while it
runs, because `calib_cam_intrinsics`, `calib_cam_extrinsics`, and
`calib_cam_timeoffset` are `true` in `estimator_config.yaml`. It corrects
only small errors. It does not work without a good start value.

### Assumptions

This page uses these assumptions. If your setup is different, change the
topic names in the commands.

- The robot streams the camera through LiveKit, and `ros_portal` publishes
  each frame as `sensor_msgs/msg/CompressedImage`.
- A split node publishes the left lens on `/insta/cam0/image_raw/compressed`
  and the right lens on `/insta/cam1/image_raw/compressed`. Both images of a
  pair have the same `header.stamp`.
- The robot IMU arrives as `sensor_msgs/msg/Imu` on `/insta/imu/data_raw`.
- The robot stamps the video frames and the IMU messages with one clock.

The ScaRF-SLAM launch file and its OpenVINS configuration use the `/insta/...`
topic names. If you use the same names, you change fewer files.

### Prerequisites

1. Mount the camera rigidly to the IMU. If the camera moves relative to the
   IMU after calibration, do the calibration again.

2. Set the camera to the resolution and frame rate that you will use in the
   demo. The intrinsics are valid for one resolution only.

3. Print an AprilGrid target (a board of square AprilTag markers) from the
   [Kalibr calibration targets](https://github.com/ethz-asl/kalibr/wiki/calibration-targets)
   page. Glue it to a flat, rigid board.

4. Measure the printed tags with a ruler. Write a target file with your
   values, for example `april_6x6.yaml`:

   ```yaml
   target_type: 'aprilgrid'
   tagCols: 6
   tagRows: 6
   tagSize: 0.088   # edge of one tag, in meters
   tagSpacing: 0.3  # gap between tags, as a fraction of tagSize
   ```

5. Build the Kalibr Docker image on the host. Kalibr uses ROS 1, so it does
   not run in the ROS 2 devcontainer:

   ```bash
   git clone https://github.com/ethz-asl/kalibr.git
   cd kalibr
   docker build -t kalibr -f Dockerfile_ros1_20_04 .
   ```

6. Make a data folder on the host for the bags and the results, for example
   `~/calib`. Put `april_6x6.yaml` in this folder.

### Step 1: Record The Bags

You record the bags in the devcontainer, through the same LiveKit path that
the demo uses. Then the calibration includes the effects of the video codec
and the stream resolution.

Kalibr reads `sensor_msgs/Image` reliably. You decompress the images before
you record them.

1. Start `ros_portal` and the split node, as you do for the demo.

2. Decompress both camera topics, each in its own shell:

   ```bash
   ros2 run image_transport republish compressed raw --ros-args \
     -r in/compressed:=/insta/cam0/image_raw/compressed -r out:=/insta/cam0/image_raw
   ros2 run image_transport republish compressed raw --ros-args \
     -r in/compressed:=/insta/cam1/image_raw/compressed -r out:=/insta/cam1/image_raw
   ```

3. Record the IMU noise bag. Put the IMU on a still surface, and do not touch
   it for at least 3 hours:

   ```bash
   ros2 bag record -o imu_static /insta/imu/data_raw
   ```

4. Record the camera bag. Move the target slowly in front of the camera for
   about 2 minutes. Show the target in all areas of each image, and at
   different distances and angles. Keep the target in the overlap area of the
   two lenses for part of the time.

   ```bash
   ros2 bag record -o cam_stereo /insta/cam0/image_raw /insta/cam1/image_raw
   ```

5. Record the camera and IMU bag. Keep the target still, and move the camera
   and IMU together for about 1 minute. Rotate around all three axes and move
   along all three axes. Move fast enough to excite the IMU, but not so fast
   that the images blur.

   ```bash
   ros2 bag record -o cam_imu /insta/cam0/image_raw /insta/cam1/image_raw /insta/imu/data_raw
   ```

6. Convert the ROS 2 bags to ROS 1 bags with `rosbags-convert`. The
   `rosbags` package is in `/opt/scarf_venv`:

   ```bash
   rosbags-convert --src imu_static --dst ~/calib/imu_static.bag
   rosbags-convert --src cam_stereo --dst ~/calib/cam_stereo.bag
   rosbags-convert --src cam_imu --dst ~/calib/cam_imu.bag
   ```

   If `~/calib` is not on the host mount, copy the `.bag` files to the host
   data folder.

### Step 2: Measure The IMU Noise

You find the IMU noise values with an Allan variance analysis. This analysis
measures the sensor noise from a long still recording.

1. Run [allan_variance_ros](https://github.com/ori-drs/allan_variance_ros) on
   `imu_static.bag`. Follow the instructions in its README.

2. The tool writes an `imu.yaml` file. Make sure that it has these fields, and
   set `rostopic` and `update_rate` to your values:

   ```yaml
   rostopic: /insta/imu/data_raw
   update_rate: 200.0
   accelerometer_noise_density: 0.0
   accelerometer_random_walk: 0.0
   gyroscope_noise_density: 0.0
   gyroscope_random_walk: 0.0
   ```

3. Put `imu.yaml` in the host data folder.

Measured noise values are often too small for OpenVINS, because they do not
include vibration and temperature changes. If OpenVINS diverges, multiply the
four noise values by a factor from 2 to 10.

If the IMU vendor publishes noise values, you can use them as a start value
instead of this step.

### Step 3: Calibrate The Camera Intrinsics And Stereo Extrinsics

1. Start the Kalibr container on the host:

   ```bash
   xhost +local:root
   docker run -it -e "DISPLAY" -e "QT_X11_NO_MITSHM=1" \
     -v "/tmp/.X11-unix:/tmp/.X11-unix:rw" \
     -v "$HOME/calib:/data" kalibr
   ```

2. In the container, source the Kalibr workspace:

   ```bash
   source devel/setup.bash
   ```

3. Calibrate both cameras with the equidistant fisheye model:

   ```bash
   rosrun kalibr kalibr_calibrate_cameras \
     --bag /data/cam_stereo.bag \
     --target /data/april_6x6.yaml \
     --models pinhole-equi pinhole-equi \
     --topics /insta/cam0/image_raw /insta/cam1/image_raw \
     --bag-freq 10.0
   ```

   Use `pinhole-equi`. OpenVINS supports only the `radtan` and `equidistant`
   distortion models, and `radtan` does not fit a 180° fisheye lens.

4. Open the PDF report. Make sure that the reprojection error is below 1
   pixel for each camera.

Kalibr writes `camchain-cam_stereo.yaml` in `/data`.

If the two lenses do not see the target at the same time, Kalibr cannot
connect the two cameras. In this case, record a new camera bag with more time
in the overlap area.

#### Use An Existing OpenCV Stereo Calibration

If you calibrated the stereo pair with the OpenCV ChArUco tool, you do not do
Step 3 in Kalibr. You convert `stereo_calibration.json` to a Kalibr camchain
file instead. The OpenCV fisheye model is the Kalibr `pinhole-equi` model, so
the values do not change. cam0 is the left lens.

1. In the devcontainer, convert the calibration:

   ```bash
   ros2 run livekit_scarf_slam convert_stereo_calibration.py kalibr \
     stereo-calibration/stereo_calibration.json ~/calib/camchain-stereo.yaml
   ```

2. In Step 4, pass `--cam /data/camchain-stereo.yaml`.

The camchain uses the compressed topics by default, because Kalibr reads
`sensor_msgs/msg/CompressedImage`. With compressed topics, you do not run
`image_transport republish`, and the bag is much smaller. Record the
`cam_imu` bag in Step 1 with the compressed topics:

```bash
ros2 bag record -o cam_imu /insta/cam0/image_raw/compressed \
  /insta/cam1/image_raw/compressed /insta/imu/data_raw
```

If you record other topic names, pass `--cam0-topic` and `--cam1-topic` to
the script. The stereo demo streams one SBS topic, but Kalibr needs one
topic for each camera. Split the SBS topic into two topics before you record
the bag. The `cam_stereo` bag is not necessary for this path. The Kalibr
camera-IMU step needs an AprilGrid target, because Kalibr does not read
ChArUco boards.

### Step 4: Calibrate The Camera To The IMU

1. In the same Kalibr container, run the camera and IMU calibration:

   ```bash
   rosrun kalibr kalibr_calibrate_imu_camera \
     --bag /data/cam_imu.bag \
     --target /data/april_6x6.yaml \
     --cam /data/camchain-cam_stereo.yaml \
     --imu /data/imu.yaml
   ```

2. Open the PDF report. Make sure that the reprojection error is below 1
   pixel. Make sure that the gyroscope and accelerometer errors stay inside
   their bounds.

3. Make sure that the translation in each `T_cam_imu` matches the distance
   between the IMU and the lens that you measure with a ruler.

Kalibr writes `camchain-imucam-cam_imu.yaml` in `/data`.

### Step 5: Update The Configuration Files

You copy the configuration files from ScaRF-SLAM to `config/` in this
package, and you put your values in the copies.

1. In `config/open_vins/kalibr_imucam_chain.yaml`, replace the `cam0` and
   `cam1` blocks with the blocks from `camchain-imucam-cam_imu.yaml`. Keep
   these fields: `T_cam_imu`, `cam_overlaps`, `camera_model`,
   `distortion_coeffs`, `distortion_model`, `intrinsics`, `resolution`,
   `rostopic`, and `timeshift_cam_imu`.

2. Set `rostopic` in each camera block to the raw image topic that OpenVINS
   reads, `/insta/cam0/image_raw` or `/insta/cam1/image_raw`.

3. In `config/open_vins/kalibr_imu_chain.yaml`, set the four noise values,
   `rostopic`, and `update_rate` from `imu.yaml`. Keep `T_i_b`, `Tw`,
   `R_IMUtoGYRO`, `Ta`, `R_IMUtoACC`, and `Tg` at their identity and zero
   values.

4. In `config/open_vins/estimator_config.yaml`, set `max_cameras: 2`.
   If the two lenses overlap, set `use_stereo: true`.

5. Replace `mask_cam0.png` and `mask_cam1.png` with masks for your camera.
   Each mask has the same resolution as its camera image. OpenVINS does not
   track features in the white areas. Paint the robot body and the black
   border outside the fisheye circle white.

6. If you do not make masks, set `use_mask: false` in
   `estimator_config.yaml`.

7. In `config/scarf_slam/indoor_ori_slam.yaml`, replace the `fisheye_cam0`
   and `fisheye_cam1` blocks with the same values as in step 1. Do not copy
   `rostopic` or `timeshift_cam_imu` to these blocks.

8. In the same file, set `pinhole_intrinsics` and `pinhole_resolution`.
   ScaRF-SLAM undistorts each fisheye image to this virtual pinhole camera
   before depth estimation. Start with the `fx` and `fy` of `cam0`, and put
   the image center in the middle of `pinhole_resolution`.

### Step 6: Test The Calibration

1. Start the demo with your configuration files. Use the commands in the
   `README.md` of this package.

2. Hold the camera still for about 2 seconds, so that OpenVINS can
   initialize.

3. Walk a closed loop of about 10 meters, and stop at the start point.

4. Make sure that the OpenVINS trajectory in RViz ends near the start point.
   Make sure that the scale matches the distance that you walked.

If the trajectory drifts fast or jumps, do these checks:

- Make sure that the `stamp_fallbacks` diagnostic of `ros_portal` stays at
  zero. If it increases, the image stamps come from the receive time and not
  from the robot clock.
- Make sure that the IMU axes in `T_cam_imu` match the axes of the IMU on the
  robot.
- Increase the IMU noise values, as described in step 2.

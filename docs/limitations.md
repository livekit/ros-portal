# Current Limitations

## Video Tracks
- `VideoSource` and `LocalVideoTrack` are created from the first received image
  dimensions. If camera resolution changes mid-stream, the track is not
  recreated.
- Only `rgba8`, `rgb8`, `bgr8`, `bgra8`, and `mono8` encodings are handled.
  Other encodings are dropped with a throttled warning.
- Encoding conversion is done per-pixel on the CPU inside the subscription
  callback. High-resolution or high-framerate streams may need a more efficient
  path.

## Audio Tracks

No ROS2 message type is currently mapped to a LiveKit audio track. Candidate
types include `audio_common_msgs/msg/AudioData` and raw PCM topics.

## General

- Once a subscription is created, it is never removed, even if the publisher
  disappears.
- Config changes require the installed YAML file to be refreshed. Rebuild after
  editing the source YAML, or use `--symlink-install` during development.
- CMake linking around LiveKit SDK artifacts is still more fragile than it
  should be and depends on expected SDK artifact layout.

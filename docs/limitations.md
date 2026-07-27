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

## ROS Distributions

- On Humble, do not destroy the bridge node while an executor it was added to is
  still spinning. Humble's executor keeps referring to a removed node's rmw
  entities and notify guard condition, so a concurrent `rcl_wait` can crash and a
  later wait-set rebuild throws `guard condition implementation is invalid`.
  Cancel the executor and join its spin thread first; if the executor also hosts
  nodes that must keep running, replace it rather than resume it. Jazzy and newer
  are unaffected — their executors take shared ownership of what they wait on.
- Message schemas use rosbag2's renderer where available and a bundled
  byte-compatible fallback otherwise (currently Humble). Unit tests compare both
  renderers wherever the rosbag2 API is available.
- Schema identity is a SHA-256 over the rendered definition text, and that text
  is the `.msg` files as shipped, comments included. Distributions do not ship
  byte-identical `.msg` files — `builtin_interfaces/msg/Time.msg`, for example,
  has a trailing space on one comment line in Jazzy that Humble does not have —
  so bridges on different distributions can compute different schema hashes for
  the same message type and reject each other's data tracks. This is inherent to
  hashing the definition text and is not specific to the Humble path; keep both
  ends of a session on the same distribution.

## General

- Once a subscription is created, it is never removed, even if the publisher
  disappears.
- Config changes require the installed YAML file to be refreshed. Rebuild after
  editing the source YAML, or use `--symlink-install` during development.
- CMake linking around LiveKit SDK artifacts is still more fragile than it
  should be and depends on expected SDK artifact layout.

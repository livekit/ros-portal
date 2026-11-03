# Configuration

ROS Portal is configured via environment variables for LiveKit parameters and
a YAML configuration file for runtime parameters.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Environment Variables](#environment-variables)
  - [Token Configuration](#token-configuration)
    - [Token requirements](#token-requirements)
    - [Literal token](#literal-token)
    - [Endpoint token server](#endpoint-token-server)
    - [Development token server](#development-token-server)
- [Configuration File](#configuration-file)
  - [Examples](#examples)
  - [Minimal Config](#minimal-config)
  - [Top-Level Fields](#top-level-fields)
  - [Services](#services)
  - [Topics](#topics)
    - [Preserving the publisher identity](#preserving-the-publisher-identity)
    - [Capping the outbound forward rate](#capping-the-outbound-forward-rate)
    - [Latched topics (`latched: true`)](#latched-topics-latched-true)
    - [Data track encoding (`encoding`)](#data-track-encoding-encoding)
    - [Outbound video](#outbound-video)
    - [Inbound video](#inbound-video)
  - [Video Options](#video-options)

## Prerequisites

Follow [Installation](./installation.md) before this guide.

This guide assumes the following:

- LiveKit CLI is installed and can be run via `lk`
- If using LiveKit Cloud, a project is setup with an API key and secret available

## Environment Variables

Environment variables are used to configure ROS Portal's connection and access to a
LiveKit server.

### Token Configuration

ROS Portal supports literal tokens, an endpoint token server, and a development token server.
Select **one** of the following options:

| Token source | Environment variables | Use case | Token issuer |
|---|---|---|---|
| [Literal token](#literal-token) | `LIVEKIT_URL` + `LIVEKIT_TOKEN`<br><br>`LIVEKIT_API_KEY` + `LIVEKIT_API_SECRET` (optional - LiveKit Cloud) | Local LiveKit development, or direct control over token grants and lifetime | User, via `lk token create` or another token-generation service |
| [Endpoint token server](#endpoint-token-server) | `LIVEKIT_TOKEN_ENDPOINT` | Production, server authenticates requests and mints tokens | User-developed backend |
| [Development token server](#development-token-server) | `LIVEKIT_TOKEN_SERVER_ID` | Quick prototypes and testing with LiveKit Cloud only; never use in production | LiveKit Cloud |

Note the following:

- ROS Portal fails to start if more than one of `LIVEKIT_TOKEN`, `LIVEKIT_TOKEN_ENDPOINT`,
and `LIVEKIT_TOKEN_SERVER_ID` is set.
- `LIVEKIT_API_KEY` and `LIVEKIT_API_SECRET` are used by LiveKit CLI to mint literal tokens
against a LiveKit Cloud project. ROS Portal does not read either variable, only the resulting
`LIVEKIT_TOKEN`.

#### Token requirements

Any token minted by the user (literal) or the user's endpoint backend must grant room join,
identify the participant and room, and allow the participant to update its
metadata. The LiveKit Cloud development token server handles these grants.

| Option/Grant | `lk` flag | Description |
|---|---|---|
| Room join | `--join` | Connect to the LiveKit room. |
| Room name | `--room <name>` | Identifies the room to join. |
| Identity | `--identity <identity>` | Identifies the ROS Portal participant. Example: `ros-portal-robot` |
| `canUpdateOwnMetadata` | `--allow-update-metadata` | ROS Portal advertises its LiveKit participant with the `lk.robot` attribute set to `"true"`. |

#### Literal token

Set `LIVEKIT_URL` and a participant JWT in `LIVEKIT_TOKEN`. For a local LiveKit
server started with `--dev`:

```bash
export LIVEKIT_URL=ws://127.0.0.1:7880
export LIVEKIT_TOKEN="$(lk token create \
  --join \
  --room robo_room \
  --identity ros-portal \
  --allow-update-metadata \
  --valid-for 24h \
  --token-only \
  --yes \
  --dev)"
```

For LiveKit Cloud, set the project credentials before minting the token:

```bash
export LIVEKIT_URL=wss://<project details>.livekit.cloud
export LIVEKIT_API_KEY=<api-key>
export LIVEKIT_API_SECRET=<api-secret>
export LIVEKIT_TOKEN="$(lk token create \
  --join \
  --room robo_room \
  --identity ros-portal \
  --allow-update-metadata \
  --valid-for 24h \
  --token-only \
  --yes)"

# Then run ROS Portal
```

#### Endpoint token server

For production, point ROS Portal at a backend
[token endpoint](https://docs.livekit.io/frontends/build/authentication/endpoint/)
that returns the LiveKit server URL and a freshly minted participant JWT:

```bash
export LIVEKIT_TOKEN_ENDPOINT=https://example.com/livekit/token

# Then run ROS Portal
```

#### Development token server

For development or testing with LiveKit Cloud, enable a
[development token server](https://docs.livekit.io/frontends/build/authentication/development-token-server/)
in project settings and export its token server ID:

```bash
export LIVEKIT_TOKEN_SERVER_ID=token-server-xxxxxx

# Then run ROS Portal
```

This source is insecure and must not be used in production.

## Configuration File

The YAML configuration file defines which ROS topics and services ROS Portal
forwards through LiveKit, in which direction, and related runtime options.
The configuration file path is resolved through the node's `config_path` ROS
parameter. See [Running](./running.md) for more information.

`config_path` is optional. When it is empty or omitted, ROS Portal applies a
builtin default that forwards all topics bidirectionally (equivalent to
[`all_topics.yaml`](../src/ros_portal/config/all_topics.yaml)).

Follow the examples below to get started quickly, or use the field reference
further down to build out a full configuration file.

### Examples

Complete configs in this repository:

- [`all_topics.yaml`](../src/ros_portal/config/all_topics.yaml) — mirrors the builtin default applied when no `config_path` is provided; forwards all topics bidirectionally.
- [`ros_portal.yaml`](../src/ros_portal/config/ros_portal.yaml) — example package config used for development and integration testing.
- [`turtle_sim_config.yaml`](../src/ros_portal_tutorials/config/turtle_sim_config.yaml)
  and
  [`turtle_sim_controller.yaml`](../src/ros_portal_tutorials/config/turtle_sim_controller.yaml) —
  paired turtlesim tutorial configs (see [Tutorials](./tutorials.md)).

Full robot stacks with ROS Portal configs:

- `cobra_flex_ros` [`livekit.yaml`](https://github.com/livekit-examples/cobra_flex_ros/blob/main/bringup/config/livekit.yaml) —
  Waveshare Cobra Flex teleoperation.
- `waver_ros` [`livekit_robot.yaml`](https://github.com/livekit-examples/waver_ros/blob/main/src/waver_bringup/config/livekit_robot.yaml)
  and
  [`livekit_controller.yaml`](https://github.com/livekit-examples/waver_ros/blob/main/src/waver_bringup/config/livekit_controller.yaml) —
  WAVE ROVER robot and controller sides.

### Minimal Config

```yaml
ros_portal:
  version: "0.0.1"
```

### Top-Level Fields

All config lives under `ros_portal`.

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `$schema` | string | no | - | Link to JSON schema. Use `https://raw.githubusercontent.com/livekit/ros-portal/main/src/ros_portal_config/schema/ros_portal_config.schema.json` and an IDE YAML plugin to validate config files and add autocomplete. |
| `version` | string | yes | - | Configuration schema version, currently `"0.0.1"`. |
| `ros_threads` | integer | no | `0` | Standalone ROS executor thread count. `0` uses the available CPU-core count, matching `rclcpp` default. Ignored in composition, where the container owns the executor. |
| `enable_all_ros_topic_stats` | boolean | no | `false` | Enable ROS 2 topic statistics for every outbound topic subscription. Overrides per-topic values when true. |
| `services` | list | no | `[]` | Service route declarations. |
| `topics` | list | no | `[]` | Topic route declarations. |
| `video_sources` | list | no | `[]` | Independent capture sources published as LiveKit video tracks. |

### Services

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `service` | string | yes | - | ROS service name. Must be non-empty. |
| `direction` | string | yes | - | `out` — the only supported value. |
| `participant` | string | yes | - | LiveKit participant identity. Must be non-empty. |
| `msg_type` | string | yes | - | ROS service type, such as `std_srvs/srv/SetBool`. Must be non-empty. |

`direction: "out"` creates a local ROS service server that forwards calls to
the configured LiveKit participant using `msg_type`.

Services only accept `out`. `in` and `bidirectional` are **not** used for
services: ROS Portal does not attempt to mirror the ROS 2 service graph in both
directions. A service is a point-to-point request/response call, so a route is
fully described by the single participant that answers it. This is unlike
topics (see below), where `in` / `out` / `bidirectional` are meaningful and are
used to limit which streams cross ROS Portal for bandwidth reasons.

### Topics

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `topic` | string | yes | - | ROS topic pattern. Must be non-empty. Treated as an ECMAScript regex for the [DataTrack](https://docs.livekit.io/transport/data/data-tracks/) path; matched as a literal name for `max_rate_hz`, `latched`, `encoding`, and inbound video tracks. |
| `direction` | string | yes | - | `in`, `out`, or `bidirectional`. |
| `preserve_id` | boolean | no | `false` | Inbound topics only. Prefix the republished ROS topic with the publishing participant's identity. Applies to data tracks and video tracks. |
| `max_rate_hz` | number | no | - | Outbound topics only. Cap (in Hz) on the rate samples are forwarded to LiveKit; samples arriving within one period of the last forwarded one are dropped (like `topic_tools throttle messages`). Literal topic names only. |
| `latched` | boolean | no | `false` | Treat the topic as latched (see below). Literal topic names only. |
| `enable_ros_topic_stats` | boolean | no | `false` | Outbound topics only. Enable ROS 2 topic statistics for subscriptions matching this topic pattern. |
| `encoding` | string | no | `ros2msg` | Outbound topics only. `ros2msg`, `ros2idl`, or `jsonschema` — selects how data is encoded on the DataTrack (see below). Literal topic names only. |

Outgoing topics are those with `direction: "out"` or
`direction: "bidirectional"`. Incoming topics are those with `direction: "in"`
or `direction: "bidirectional"`. Topic patterns are ECMAScript regular
expressions matched against the full topic name.

Unlike services, topic direction is load-bearing: it controls which topics are
forwarded and in which direction. Forwarding only the required streams (and only
in the required direction) keeps unnecessary traffic off the LiveKit connection,
which matters for bandwidth on constrained links.

### ROS 2 topic statistics

Set `enable_ros_topic_stats: true` on an outbound or bidirectional topic to
publish statistics for its ROS subscription on `<topic>/statistics` (for example,
`/imu/statistics`):
```yaml
topics:
  - topic: "/imu"
    direction: "out"
    enable_ros_topic_stats: true
```

To enable statistics for every configured outbound subscription, set the
top-level override:

```yaml
ros_portal:
  version: "0.0.1"
  enable_all_ros_topic_stats: true
  topics:
    - topic: ".*"
      direction: "out"
```

The global setting overrides each topic's `enable_ros_topic_stats` value.
Inbound-only topics have ROS publishers rather than subscriptions, so these
settings do not apply to them. ROS Portal never enables statistics on a topic
whose name ends in `/statistics`, which prevents recursive statistics when a
broad topic pattern such as `.*` is configured. This feature requires ROS 2
Jazzy or later; Humble does not support topic statistics for generic serialized
subscriptions.

### Preserving the publisher identity

`preserve_id` applies only to inbound (`in` / `bidirectional`) topics and is
ignored for outbound topics. It defaults to `false` to preserve the original
topic name, so an inbound data track is republished under its own name (for
example, `/imu` stays `/imu`). When set
to `true`, the publishing participant's identity is prepended to the
republished ROS topic name, which prevents collisions when multiple
participants publish the same topic:

```yaml
topics:
  - topic: "/imu"
    direction: "in"
    preserve_id: true
```

```text
LiveKit data track: /imu   (from participant "robot-1")
ROS topic:          /robot_1/imu
```

The identity is sanitized into a legal ROS name token, so any character that is
not alphanumeric or `_` becomes `_` (e.g. `robot-1` → `robot_1`).

#### Capping the outbound forward rate

`max_rate_hz` applies only to outbound (`out` / `bidirectional`) topics and is
ignored for inbound topics. The behavior mirrors
[`topic_tools throttle messages`](https://github.com/ros-tooling/topic_tools#throttle):
samples are forwarded **on arrival**, but a sample is dropped if it arrives less
than one period (`1 / max_rate_hz`) after the last one that was forwarded. This
keeps a high-rate topic from saturating a constrained LiveKit link when a remote
consumer only needs updates at a lower rate — for example, throttling a robot's
`/imu` (often 30–100+ Hz) down to a viewer-friendly rate:

```yaml
topics:
  - topic: "/imu"
    direction: "out"
    max_rate_hz: 10
```

The first sample within each period is the one forwarded; later samples in the
same period are dropped. Only newly arriving samples are ever forwarded, so an
idle topic is never re-sent and the cap never acts as a rate *floor*. On a
backward clock jump (e.g. a sim-time reset) the throttle window is reset so
forwarding does not stall until the old timestamp is reached again.

Trade-offs to be aware of:

- **Effective rate tracks the input.** Because samples are gated on arrival,
  the achieved rate is at most `max_rate_hz` and is bounded by the source rate;
  a slow or bursty source produces fewer forwards than the cap.
- **Lossy for aggregate topics.** Only the first sample per period survives, so
  this is *not* a faithful downsample of topics whose messages each carry
  distinct content. `/tf` is the notable case: it aggregates transforms from
  multiple broadcasters in separate messages, so capping it can drop some
  broadcasters' transforms. It works well for large, whole-state messages where
  only the latest value matters (maps, costmaps, images-as-data, robot
  descriptions, imu, etc).

Unlike `topic` (an ECMAScript regex), `max_rate_hz` is matched by **literal
topic name** — the cap applies to a discovered topic only when its name equals
the configured `topic` string exactly.

#### Latched topics (`latched: true`)

Some ROS topics are **latched**: they are published once (or rarely) with
`RELIABLE` + `TRANSIENT_LOCAL` durability, and DDS replays the last sample to
subscribers that join later. `/tf_static` is the canonical example — static
transforms are broadcast once at startup, and a node that starts afterward still
receives them.

[LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/) are
**not** latched: a frame pushed before a peer subscribes is lost, and a static
topic never republishes it. Setting `latched: true` routes the topic off the
data-track path and onto a reliable
[**RPC**](https://docs.livekit.io/transport/data/rpc/) push-with-ack mechanism
instead:

- **Outbound** (`out` / `bidirectional`): ROS Portal subscribes with
  `TRANSIENT_LOCAL` QoS (so it captures state published before ROS Portal
  started), stores the topic's distinct messages, and a background worker pushes
  them to every peer ROS Portal node in the room until each acknowledges them
  over RPC. State is re-pushed only when it changes or a peer rejoins, so
  steady-state traffic is zero. A peer that repeatedly fails (for example, a
  participant without a ROS Portal handler) is dropped after a few attempts and
  retried only when new state arrives.
- **Inbound** (`in` / `bidirectional`): ROS Portal republishes received messages
  on a `TRANSIENT_LOCAL` publisher, so ROS subscribers that start after ROS
  Portal (RViz, Foxglove, and tf listeners) still receive the latched state.
  The ROS message type travels in the RPC payload, so ROS Portal creates the
  republishing publisher without any additional type configuration.

Like `max_rate_hz`, `latched` is matched by **literal topic name**, not regex.

**Payload limit.** The push uses a
[LiveKit RPC](https://docs.livekit.io/transport/data/rpc/) whose payload is capped at
**15 KiB** (base64 leaves ~11 KiB of message bytes per call). A single message
larger than that cannot be sent and is skipped with a logged warning. This is
ample for typical `/tf_static` trees (a few hundred short-named transforms), but
large monolithic latched topics (e.g. `/robot_description`, large `/map`) are out
of scope for this path — keep them on the
[DataTrack path](https://docs.livekit.io/transport/data/data-tracks/) or handle them out of
band.

```yaml
topics:
  # dynamic transforms stay on the DataTrack path
  - topic: "/tf"
    direction: "out"

  # static transforms are latched
  - topic: "/tf_static"
    direction: "out"
    latched: true
```

On the consuming ROS Portal node:

```yaml
topics:
  - topic: "/tf_static"
    direction: "in"
    latched: true
```

#### Data track encoding (`encoding`)

`encoding` applies only to outbound (`out` / `bidirectional`) topics and selects
how this ROS Portal node encodes a topic's messages on the LiveKit DataTrack
and which schema it advertises to subscribers:

| Value | Frame format | Advertised schema | Use case |
|---|---|---|---|
| `ros2msg` (default) | ROS CDR | ROS 2 Message | ROS-to-ROS forwarding; no conversion cost. |
| `ros2idl` | ROS CDR | ROS 2 IDL | ROS-to-ROS forwarding where consumers expect an IDL schema. |
| `jsonschema` | JSON | generated JSON Schema | Communicating with **non-ROS** systems that read JSON directly. |

```yaml
topics:
  # Publish /diagnostics as JSON so a non-ROS dashboard can consume it directly.
  - topic: "/diagnostics"
    direction: "out"
    encoding: "jsonschema"
```

Notes:

- `jsonschema` deserializes each ROS message and sends a JSON frame described by
  a JSON Schema generated from the ROS type. A peer ROS Portal node receiving
  the track transparently converts the JSON back into the ROS message before
  republishing, so ROS-to-ROS forwarding still works with any encoding.
- `ros2idl` requires the local ROS type definition to be renderable as ROS 2
  IDL. A topic requesting an encoding whose schema cannot be rendered is skipped
  with an error.
- Inbound decoding is independent of this setting: it is auto-detected from the
  remote track's advertised frame encoding (CDR or JSON). `encoding` is ignored
  for pure inbound (`in`) topics.
- Like `max_rate_hz` and `latched`, `encoding` is matched by **literal topic
  name**, not regex.

#### Outbound video

ROS Portal sends an outbound topic of type `sensor_msgs/Image` or
`sensor_msgs/CompressedImage` as a LiveKit video track. Other types use a data
track.

- `sensor_msgs/Image` supports `rgba8`, `rgb8`, `bgr8`, `bgra8`, and `mono8`.
  The track has the same name as the topic.
- `sensor_msgs/CompressedImage` supports JPEG only. The `format` field must
  contain `jpeg` or `jpg` (case-insensitive).
  ROS Portal decodes each frame before LiveKit encodes it as video.
- Video tracks follow the same naming convention as data tracks.

```yaml
topics:
  # Sends the LiveKit video track /insta/cam0/image_raw/compressed.
  - topic: "/insta/cam0/image_raw/compressed"
    direction: "out"
```

The track size comes from the first frame.

#### Inbound video

ROS Portal republishes a remote LiveKit video track as a
`sensor_msgs/CompressedImage` topic with JPEG data. A video track is forwarded
when its name exactly matches the `topic` of an `in` or `bidirectional` entry.
Video tracks never match by regex. For example, the entry `/camera/.*`
forwards matching data tracks but no video tracks.

A sending ROS Portal publishes each `sensor_msgs/Image` topic as a video track
with the same name. On the receiving host, add an entry with that name:

```yaml
topics:
  # On the remote ROS portal node receive the edge camera as JPEG
  - topic: "/camera/image_raw"
    direction: "in"
```

```text
LiveKit video track: /camera/image_raw   (from participant "edge-robot")
ROS topic:           /camera/image_raw
ROS type:            sensor_msgs/msg/CompressedImage (format "jpeg")
```

With `preserve_id: true`, the topic is `/edge_robot/camera/image_raw`.

Video tracks use the same topic naming as data tracks: the track name becomes
an absolute ROS topic name, with an optional participant prefix. ROS Portal
does not add or remove `/compressed`. A track named
`/camera/image_raw/compressed` is published on that same ROS topic (or
`/edge_robot/camera/image_raw/compressed` with `preserve_id: true`). Configure
consumers for `sensor_msgs/CompressedImage`, even when the topic name has no
`/compressed` suffix.

ROS Portal keeps the sender's `header.stamp` and `header.frame_id`. The sending
ROS Portal attaches both to each video frame as LiveKit frame metadata. The
receiving ROS Portal copies them into the `CompressedImage` header.

If a frame has no metadata, ROS Portal uses fallback values. This happens when
the sender is not ROS Portal, for example a browser. The stamp then comes from
the receiver's ROS clock when the frame arrives. The frame_id is the track name
without the leading `/`. The `inbound.video_stamp_fallbacks` diagnostic counts
these frames (see [diagnostics.md](diagnostics.md#topic_forwarder)).

Notes:

- LiveKit delivers decoded frames only. The receiving ROS Portal encodes each
  frame again as JPEG at quality 90. This costs CPU on the receiving host and
  adds JPEG loss to the video codec loss.
- If JPEG encoding is slower than the frame rate, ROS Portal drops the oldest
  buffered frame. Frames do not queue without limit.
- While the output topic has no ROS subscribers, ROS Portal does not encode
  frames. After at most 0.5 s, it also pauses the LiveKit track, so the server
  stops sending it. When a subscriber appears, ROS Portal resumes the track
  within 0.5 s. The server then asks the sender for a key frame, so the first
  image arrives about one network round trip later. A `rosbag2` recorder
  counts as a subscriber.
- ROS Portal unsubscribes from every remote audio track and from every video
  track that does not match an inbound topic. The LiveKit SDK decodes each
  subscribed track even when nothing reads it, so this saves bandwidth and
  CPU.
- The publisher uses reliable QoS with depth 10. If the output topic matches the
  `best_effort_qos_topics` ROS parameter, the publisher uses the sensor data QoS
  profile (best effort, depth 5).
- The sender omits a frame_id longer than 255 bytes. The receiver then uses the
  fallback frame_id.

### Video Options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `bitrate_kbps` | integer | no | Target bitrate in kbps. Must be positive. |
| `codec` | string | no | Video codec name. Must be non-empty when set. |

## Video Sources

`video_sources` publishes video independently of the ROS topic graph. Every
entry owns one capture producer, one LiveKit video track, and one diagnostic
status. Sources are created, published, and started after ROS Portal connects to
the room. A failure affects only that entry: ROS Portal and other configured
sources continue running, while `video_source/<track_name>/<index>` reports the
error.

```yaml
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "front_camera"
      source:
        type: "gstreamer"
        pipeline: >-
          v4l2src device=/dev/video0 ! videoconvert !
          x264enc name=lk_encoder tune=zerolatency ! h264parse !
          appsink name=lk_appsink
        codec: "h264"
        resolution:
          width: 1280
          height: 720
        rate_control:
          element: "lk_encoder"
          property: "bitrate"
          unit: "kbps"
      publish_options:
        max_bitrate_bps: 3500000
        max_framerate: 30
```

### Video source entry

| Field | Type | Required | Description |
|---|---:|---:|---|
| `track_name` | string | yes | Non-empty LiveKit video track name. |
| `simulcast` | boolean | no | Publish the track with simulcast enabled. Defaults to `false`. |
| `source` | map | yes | Capture backend and its configuration. |
| `publish_options` | map | no | Application-controlled LiveKit publish limits. |

All capture-backed tracks are published with LiveKit track source `camera`.
Capture-derived settings take precedence where required: encoded GStreamer
ingest dictates its codec, disables simulcast, and selects the pre-encoded
encoder backend. Because of that precedence, `simulcast` only takes effect for
pixel sources (`pattern` and `clock`); a `gstreamer` source publishes the single
pre-encoded layer it produces regardless of the setting.

### Source configuration

| Field | Type | Required | Description |
|---|---:|---:|---|
| `type` | string | yes | `gstreamer`, `pattern`, or `clock`. |
| `pipeline` | string | GStreamer only | GStreamer launch description. Must contain `appsink name=lk_appsink` or leave exactly one encoded video source pad unlinked. |
| `codec` | string | GStreamer only | `h264`, `h265`, `vp8`, `vp9`, or `av1`. Inferred from GStreamer caps when omitted. |
| `resolution` | map | GStreamer only | Positive `width` and `height`. Discovered from negotiated caps when omitted; verified against the stream when supplied. |
| `rate_control` | map | GStreamer only | Forward WebRTC bitrate targets to a GStreamer encoder property. |
| `pattern` | map | no | Pattern-only rendering and output characteristics. Optional when `type` is `pattern`. |
| `clock` | map | no | Clock-only output characteristics. Optional when `type` is `clock`. |

Pattern and clock sources have default resulition and FPS: 640x480 at 30 fps defaults.
GStreamer uses the pipeline's negotiated caps for omitted codec and
resolution settings; source creation fails if required characteristics cannot
be determined.

`rate_control` has three required fields:

| Field | Type | Description |
|---|---:|---|
| `element` | string | Name of the encoder element in the pipeline, such as `lk_encoder`. |
| `property` | string | Writable integer bitrate property, such as `bitrate` or `target-bitrate`. |
| `unit` | string | `bps` or `kbps`, matching the encoder property. |

Without `rate_control`, the pipeline encoder retains the fixed bitrate supplied
in the launch description. Downstream keyframe requests are still forwarded to
the pipeline.

### Publish options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `max_bitrate_bps` | integer | no | Positive maximum advertised publish bitrate in bits per second. |
| `max_framerate` | integer | no | Positive maximum advertised publish frame rate. |

These correspond to the bitrate and frame-rate limits used by the Rust capture
examples. They do not configure the GStreamer encoder itself; use the pipeline
properties and optional `rate_control` binding for that.

ROS Portal supports the SDK's GStreamer, pattern, and clock sources. Encoded
sources apply their required codec and encoder settings through the SDK's
`publishOptions()` method. Pixel sources preserve the configured publish limits
and simulcast setting. LiveKit credentials remain process environment settings,
and source diagnostics are always enabled.

### Pattern source

The built-in deterministic pattern source renders a synthetic test pattern and
accepts no GStreamer fields. It is intended for testing:

```yaml
video_sources:
  - track_name: "pattern_camera"
    source:
      type: "pattern"
```

The rendered pattern defaults to `gradient` and output characteristics to 640x480
at 30 fps. Override any of them with an optional `pattern` block:

| Field | Type | Required | Description |
|---|---:|---:|---|
| `pattern` | string | no | `gradient` (animated color gradient) or `logo` (bouncing LiveKit logo). Defaults to `gradient`. |
| `resolution` | map | no | Positive `width` and `height`. Defaults to 640x480. |
| `framerate_fps` | integer | no | Positive frame rate. Defaults to 30. |

```yaml
video_sources:
  - track_name: "pattern_camera"
    source:
      type: "pattern"
      pattern:
        pattern: "logo"
        resolution: { width: 1280, height: 720 }
        framerate_fps: 15
```

### Clock source

The GPU-rendered wall clock shows local time on the machine running ROS Portal,
with millisecond precision. It defaults to 640x480 at 30 fps. An optional `clock`
block accepts positive `resolution.width`, `resolution.height`, and
`framerate_fps` values:

```yaml
video_sources:
  - track_name: "clock"
    source:
      type: "clock"
      clock:
        resolution: { width: 1280, height: 720 }
        framerate_fps: 30
```

Pattern and clock sources require a compatible GPU adapter, including a software
Vulkan adapter for headless testing. GStreamer sources require the pipeline's
runtime plugins instead.

Audio options are currently not supported.

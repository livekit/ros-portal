# Configuration

ROS Portal is configured via environment variables for LiveKit parameters and
a YAML configuration file for runtime parameters.

## Prerequisites

Follow [Installation](./installation.md) before this guide.

This guide assumes the following:

- LiveKit CLI is installed and can be run via `lk`
- If using LiveKit Cloud, a project is setup with an API key and secret available

## Environment Variables

Environment variables are used to configure ROS Portal's connection and access to a
LiveKit server.

| Variable | Required | Description |
|---|---:|---|
| `LIVEKIT_URL` | yes | WebSocket URL of the LiveKit server. |
| `LIVEKIT_TOKEN` | yes | LiveKit access JWT for the ROS Portal participant. |
| `LIVEKIT_API_KEY` | no | LiveKit Cloud only: API key used to mint `LIVEKIT_TOKEN` with LiveKit CLI (not read by ROS Portal). |
| `LIVEKIT_API_SECRET` | no | LiveKit Cloud only: API secret used to mint `LIVEKIT_TOKEN` with LiveKit CLI (not read by ROS Portal). |

### Token Configuration

`LIVEKIT_TOKEN` allows ROS Portal to connect to the LiveKit room and is minted
using `lk token create`. The following table lists the required token options:

| Option/Grant | `lk` flag | Why ROS Portal needs it |
|---|---|---|
| Room join | `--join` | Connect to the LiveKit room. |
| Room name | `--room <name>` | Identifies the room to join. |
| Identity | `--identity <identity>` | Identifies the ROS Portal participant. |
| `canUpdateOwnMetadata` | `--allow-update-metadata` | ROS Portal advertises its LiveKit participant with the `lk.robot` attribute set to `"true"`. |

### Development Server Examples

Use the following two example commands to quickly configure the environment for a local
LiveKit server instance (started using `--dev`):

```bash
export LIVEKIT_URL=ws://127.0.0.1:7880
```

```bash
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

### LiveKit Cloud Examples

Use the following commands as reference for configuring the environment for a LiveKit Cloud
project:

```bash
export LIVEKIT_URL=wss://<project details>.livekit.cloud
```

Export `LIVEKIT_API_KEY` / `LIVEKIT_API_SECRET` from the project before minting `LIVEKIT_TOKEN`:

```bash
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
```

## Configuration File

The YAML configuration file defines which ROS topics and services ROS Portal
forwards through LiveKit, in which direction, and related runtime options.
The configuration file path is resolved through the node's `config_path` ROS
parameter. See [Running](./running.md) for more information.

Follow the examples below to get started quickly, or use the field reference
further down to build out a full configuration file.

### Examples

Complete configs in this repository:

- [`ros_portal.yaml`](../src/ros_portal/config/ros_portal.yaml) — default package config used for development and integration testing.
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
| `version` | string | yes | - | Configuration schema version, currently `"0.0.1"`. |
| `topic_polling_period_ms` | integer | no | `500` | ROS graph polling interval in milliseconds. Must be positive. |
| `ros_threads` | integer | no | `0` | ROS executor thread count. `0` uses the available CPU-core count, matching `rclcpp` default. |
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
| `topic` | string | yes | - | ROS topic pattern. Must be non-empty. Treated as an ECMAScript regex for the [DataTrack](https://docs.livekit.io/transport/data/data-tracks/) path; matched as a literal name for `max_rate_hz`, `latched`, and `encoding`. |
| `direction` | string | yes | - | `in`, `out`, or `bidirectional`. |
| `preserve_id` | boolean | no | `false` | Inbound topics only. Prefix the republished ROS topic with the publishing participant's identity. |
| `max_rate_hz` | number | no | - | Outbound topics only. Cap (in Hz) on the rate samples are forwarded to LiveKit; samples arriving within one period of the last forwarded one are dropped (like `topic_tools throttle messages`). Literal topic names only. |
| `latched` | boolean | no | `false` | Treat the topic as latched (see below). Literal topic names only. |
| `encoding` | string | no | `ros2msg` | Outbound topics only. `ros2msg`, `ros2idl`, or `jsonschema` — selects how data is encoded on the DataTrack (see below). Literal topic names only. |

Outgoing topics are those with `direction: "out"` or
`direction: "bidirectional"`. Incoming topics are those with `direction: "in"`
or `direction: "bidirectional"`. Topic patterns are ECMAScript regular
expressions matched against the full topic name.

Unlike services, topic direction is load-bearing: it controls which topics are
forwarded and in which direction. Only forwarding the streams you actually need
(and only in the required direction) keeps unnecessary traffic off the LiveKit
connection, which matters for bandwidth on constrained links.

#### Preserving the publisher identity

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
pixel sources (`demo` and `device`); a `gstreamer` source publishes the single
pre-encoded layer it produces regardless of the setting.

### Source configuration

| Field | Type | Required | Description |
|---|---:|---:|---|
| `type` | string | yes | `gstreamer`, `demo`, or `device`. |
| `pipeline` | string | GStreamer only | GStreamer launch description. Must contain `appsink name=lk_appsink` or leave exactly one encoded video source pad unlinked. |
| `codec` | string | GStreamer only | `h264`, `h265`, `vp8`, `vp9`, or `av1`. Inferred from GStreamer caps when omitted. |
| `resolution` | map | GStreamer only | Positive `width` and `height`. Discovered from negotiated caps when omitted; verified against the stream when supplied. |
| `rate_control` | map | GStreamer only | Forward WebRTC bitrate targets to a GStreamer encoder property. |
| `device` | map | Device only | Camera selection and requested format. Required when `type` is `device`. |
| `demo` | map | no | Demo-only output characteristics. Optional when `type` is `demo`. |

Each type accepts only its own fields: a `demo` or `device` source rejects the
GStreamer-only fields above, and a `gstreamer` source rejects the `device` and
`demo` blocks. The schema tags the union by `type` but cannot express those
rules, so they are enforced when the source is created and reported through
`video_source/<track_name>/<index>`.

When resolution is omitted, source creation waits up to five seconds for the
first encoded sample. Supplying it avoids that discovery wait, but the first
sample must still match. Codec or resolution changes after capture begins end
the source with an error because the track cannot yet be republished in place.

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

This bridge maps every capture setting currently exposed by the pinned C++ SDK:
GStreamer pipeline, codec, resolution, encoder rate-control binding, native
camera selection, and the capture-derived publish options. It also follows the
Rust example's `max_bitrate` and `fps` publish limits as `max_bitrate_bps` and
`max_framerate`. LiveKit credentials remain process environment settings, and
source diagnostics are always enabled. Rust-only TCP/RTSP/shared-memory wire
formats, frame metadata, simulcast, and encoder selection are not accepted until
equivalent C++ capture APIs exist.

### Demo source

The built-in deterministic demo source publishes cycling solid-color frames and
accepts no GStreamer fields. It is intended for testing:

```yaml
video_sources:
  - track_name: "demo_camera"
    source:
      type: "demo"
```

Output characteristics default to 640x480 at 30 fps. Override either with an
optional `demo` block:

| Field | Type | Required | Description |
|---|---:|---:|---|
| `resolution` | map | no | Positive `width` and `height`. Defaults to 640x480. |
| `framerate_fps` | integer | no | Positive frame rate. Defaults to 30. |

```yaml
video_sources:
  - track_name: "demo_camera"
    source:
      type: "demo"
      demo:
        resolution: { width: 1280, height: 720 }
        framerate_fps: 15
```

### Device source

A `device` source captures from a camera through the platform's native capture
stack — AVFoundation on macOS, V4L2 on Linux — with no GStreamer pipeline to
author and no GStreamer runtime dependency. Unlike encoded GStreamer ingest, a
device source is a pixel source encoded by WebRTC, so it gets normal WebRTC rate
control and does not accept `codec` or `rate_control`.

```yaml
video_sources:
  - track_name: "front_camera"
    source:
      type: "device"
      device:
        id: "0x8020000005ac8514"
        format:
          strategy: "closest"
          resolution: { width: 1280, height: 720 }
          framerate_fps: 30
    publish_options:
      max_framerate: 30
```

List what the host exposes, including the `id` values to paste into config:

```bash
ros2 run ros_portal capture_devices
```

#### `device`

| Field | Type | Required | Description |
|---|---:|---:|---|
| `id` | string | no | Platform-stable identifier as reported by `capture_devices`. Mutually exclusive with `index`. |
| `index` | integer | no | Platform enumeration position. Mutually exclusive with `id`. |
| `format` | map | no | Requested capture format. Omit to accept the device's default. |

With neither `id` nor `index`, the platform default device is opened. Prefer
`id`: it is stable across reboots and re-plugging on macOS, whereas `index` is
the AVFoundation device position on macOS and the `/dev/videoN` node number on
Linux, either of which can shift as devices are attached. On V4L2 the `id` *is*
the node number, so re-enumerate there rather than persisting it. Supplying both
`id` and `index` is rejected.

#### `device.format`

| Field | Type | Required | Description |
|---|---:|---:|---|
| `strategy` | string | yes | `exact`, `closest`, `highest_framerate`, or `highest_resolution`. |
| `resolution` | map | see below | Positive `width` and `height`. |
| `framerate_fps` | integer | see below | Positive frame rate. |
| `frame_format` | string | no | `i420`, `nv12`, `bgra`, `rgb24`, `bgr24`, `yuyv`, `uyvy`, `grey`, or `mjpeg`. |

Which fields a strategy accepts:

| `strategy` | `resolution` | `framerate_fps` | Behavior |
|---|---:|---:|---|
| `exact` | required | required | Fails creation unless the device offers this resolution and frame rate. Frame-rate matching is rounding-tolerant, so 30 fps is satisfied by a device advertising 30.00003 fps. |
| `closest` | required | required | Uses the device's nearest supported resolution and frame rate. |
| `highest_framerate` | optional constraint | rejected | Maximizes frame rate, considering only formats matching the supplied fields. |
| `highest_resolution` | rejected | optional constraint | Maximizes resolution, considering only formats matching the supplied fields. |

`frame_format` needs care, because only resolution and frame rate participate in
format selection — a frame format is validated and then treated as a preference
the backend may substitute:

- **macOS** accepts only `i420`, `nv12`, and `bgra`; any other value fails
  creation. It then delivers NV12 regardless of which of the three was asked for.
- **V4L2** rejects `i420` and `bgra`. For `exact` and `closest` it tries the
  requested format first and then falls back through the formats it supports.
- `nv12` is the only value both backends accept, so `exact` and `closest` default
  to it when `frame_format` is omitted.
- For `highest_framerate` and `highest_resolution` there is **no default and no
  fallback**: naming a frame format there selects it outright, which can exclude
  a camera whose top resolution is offered only as `yuyv` or `mjpeg`. Leave it
  unset unless you specifically need one format.

Platform format viability is not validated against the config host, since a
package may be cross-built for a different target. An unusable request fails when
the source is created and surfaces on the source's diagnostic.

The negotiated frame rate and frame format are not reported back by the SDK. The
negotiated resolution is, and ROS Portal logs it per source at startup:

```
Capture source 'front_camera' (device) negotiated 1280x720
```

Operationally: on Linux the process needs the relevant `/dev/video*` node
visible, which for containers means mapping the device in (devcontainer users
must add `--device`). On macOS the first open prompts for camera permission; a
denied or headless process fails source creation.

Capture requires the pinned C++ SDK source build. GStreamer is additionally
required for `type: gstreamer`, but not for `device` or `demo`. The schema's
tagged `source.type` is intentionally backend-neutral so future RTSP and other
ingestion backends can be added without changing the `video_sources` collection
shape — `device` is the first backend added under it.

Audio options are not part of config version `0.0.1`.

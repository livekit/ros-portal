# Configuration Guide
ROS Portal reads the YAML config once on node startup and builds an immutable
snapshot of the user specified config. Pass the file path with the node's
`config_path` ROS parameter.

## Credentials

LiveKit credentials are not read from the config file. Set `LIVEKIT_URL` and
`LIVEKIT_TOKEN` in the node environment. The LiveKit room name comes from the
active room connection (via the token grant), not from this config.


## Minimal Config

```yaml
ros_portal:
  version: "0.0.1"
```

The parser rejects unknown fields, empty required strings, invalid enum values,
and non-positive integer values where a positive value is required.

## Top-Level Fields

All config lives under `ros_portal`.

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `version` | string | yes | - | Must be `"0.0.1"`. |
| `topic_polling_period_ms` | integer | no | `500` | ROS graph polling interval in milliseconds. Must be positive. |
| `ros_threads` | integer | no | `0` | ROS executor thread count. `0` will use the number of cpu cores found, matching rclcpp default thread count. |
| `room_options` | map | no | `{}` | LiveKit room connection options. |
| `services` | list | no | `[]` | Service route declarations. |
| `topics` | list | no | `[]` | Topic route declarations. |
| `video_sources` | list | no | `[]` | Independent capture sources published as LiveKit video tracks. |

## Room Options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `join_retries` | integer | no | Number of join retries. Must be positive. |

## Services

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

## Topics

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

### Preserving the publisher identity

`preserve_id` applies only to inbound (`in` / `bidirectional`) topics and is
ignored for outbound topics. It defaults to `false` (as to preserve the original topic name), which republishes an
inbound data track under its own topic name (e.g. `/imu` stays `/imu`). When set
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

### Capping the outbound forward rate

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

### Latched topics (`latched: true`)

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
  them to every peer ROS Portal node in the room until each acknowledges over RPC. State
  is re-pushed only when it changes or a peer rejoins, so steady-state traffic is
  zero. A peer that keeps failing (e.g. a participant without a ROS Portal handler)
  is dropped after a few attempts and retried only when new state arrives.
- **Inbound** (`in` / `bidirectional`): ROS Portal republishes received messages
  on a `TRANSIENT_LOCAL` publisher, so ROS subscribers that start after the
  ROS consumers (RViz, Foxglove, tf listeners) still receive the latched state. The ROS
  message type travels in the RPC payload, so ROS Portal creates the
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

### Data track encoding (`encoding`)

`encoding` applies only to outbound (`out` / `bidirectional`) topics and selects
how this ROS Portal node encodes the topic's messages on the LiveKit DataTrack, along
with the schema it advertises to subscribers:

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
  a JSON Schema generated from the ROS type. A peer ROS Portal node receiving the track
  transparently converts the JSON back into the ROS message before republishing,
  so ROS-to-ROS forwarding still works with any encoding.
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
| `source` | map | yes | Capture backend and its configuration. |
| `publish_options` | map | no | Application-controlled LiveKit publish limits. |

All capture-backed tracks are published with LiveKit track source `camera`.
Capture-derived settings take precedence where required: encoded GStreamer
ingest dictates its codec, disables simulcast, and selects the pre-encoded
encoder backend.

### Source configuration

| Field | Type | Required | Description |
|---|---:|---:|---|
| `type` | string | yes | `gstreamer` or `demo`. |
| `pipeline` | string | GStreamer only | GStreamer launch description. Must contain `appsink name=lk_appsink` or leave exactly one encoded video source pad unlinked. |
| `codec` | string | no | `h264`, `h265`, `vp8`, `vp9`, or `av1`. Inferred from GStreamer caps when omitted. |
| `resolution` | map | no | Positive `width` and `height`. Discovered from negotiated caps when omitted; verified against the stream when supplied. |
| `rate_control` | map | no | Forward WebRTC bitrate targets to a GStreamer encoder property. |

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

This first bridge maps every capture setting currently exposed by the pinned
C++ SDK: GStreamer pipeline, codec, resolution, encoder rate-control binding,
and the capture-derived publish options. It also follows the Rust example's
`max_bitrate` and `fps` publish limits as `max_bitrate_bps` and
`max_framerate`. LiveKit credentials remain process environment settings, and
source diagnostics are always enabled. Rust-only TCP/RTSP/shared-memory wire
formats, frame metadata, native camera selection, simulcast, and encoder
selection are not accepted until equivalent C++ capture APIs exist.

### Demo source

The built-in deterministic demo source publishes cycling solid-color frames and
accepts no GStreamer fields. It is intended for testing:

```yaml
video_sources:
  - track_name: "demo_camera"
    source:
      type: "demo"
```

Capture currently requires the pinned C++ SDK source build and system
GStreamer. The schema's tagged `source.type` is intentionally backend-neutral
so future native camera, RTSP, and other ingestion backends can be added without
changing the `video_sources` collection shape.

Audio options are not part of config version `0.0.1`.

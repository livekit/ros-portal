# Configuration Guide
The bridge reads the YAML config once on node startup and builds an immutable
snapshot of the user specified config. Pass the file path with the node's
`config_path` ROS parameter.

## Credentials

LiveKit credentials are not read from the config file. Set `LIVEKIT_URL` and
`LIVEKIT_TOKEN` in the node environment. The LiveKit room name comes from the
active room connection (via the token grant), not from this config.


## Minimal Config

```yaml
ros2_livekit_bridge:
  version: "0.0.1"
```

The parser rejects unknown fields, empty required strings, invalid enum values,
and non-positive integer values where a positive value is required.

## Top-Level Fields

All config lives under `ros2_livekit_bridge`.

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `version` | string | yes | - | Must be `"0.0.1"`. |
| `topic_polling_period_ms` | integer | no | `500` | ROS graph polling interval in milliseconds. Must be positive. |
| `ros_threads` | integer | no | `4` | ROS executor thread count. Use `0` for the rclcpp default. Keep this greater than `1` when using remote CLI services so a pending LiveKit RPC does not occupy the only executor thread. |
| `room_options` | map | no | `{}` | LiveKit room connection options. |
| `services` | list | no | `[]` | Service route declarations. |
| `topics` | list | no | `[]` | Topic route declarations. |

## Room Options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `join_retries` | integer | no | Number of join retries. Must be positive. |

## Services

| Field | Type | Required | Description |
|---|---:|---:|---|
| `service` | string | yes | ROS service name. Must be non-empty. |
| `direction` | string | yes | `out` — the only supported value. |
| `participant` | string | yes | LiveKit participant identity. Must be non-empty. |
| `msg_type` | string | yes | ROS service type, such as `std_srvs/srv/SetBool`. Must be non-empty. |

`direction: "out"` creates a local ROS service server that forwards calls to
the configured LiveKit participant using `msg_type`.

Services only accept `out`. `in` and `bidirectional` are **not** used for
services: the bridge does not attempt to mirror the ROS 2 service graph in both
directions. A service is a point-to-point request/response call, so a route is
fully described by the single participant that answers it. This is unlike
topics (see below), where `in` / `out` / `bidirectional` are meaningful and are
used to limit which streams cross the bridge for bandwidth reasons.

## Topics

| Field | Type | Required | Description |
|---|---:|---:|---|
| `topic` | string | yes | ROS topic pattern. Must be non-empty. Treated as an ECMAScript regex for the [DataTrack](https://docs.livekit.io/transport/data/data-tracks/) path; matched as a literal name for `max_rate_hz` and `latched`. |
| `direction` | string | yes | `in`, `out`, or `bidirectional`. |
| `preserve_id` | boolean | no | Default `false`. Inbound topics only. Prefix the republished ROS topic with the publishing participant's identity. |
| `max_rate_hz` | number | no | Outbound topics only. Cap (in Hz) on the rate samples are forwarded to LiveKit; samples arriving within one period of the last forwarded one are dropped (like `topic_tools throttle messages`). Literal topic names only. |
| `latched` | boolean | no | Default `false`. Treat the topic as latched (see below). Literal topic names only. |
| `video_options` | map | no | Optional video publish settings. |

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

- **Outbound** (`out` / `bidirectional`): the bridge subscribes with
  `TRANSIENT_LOCAL` QoS (so it captures state published before the bridge
  started), stores the topic's distinct messages, and a background worker pushes
  them to every peer bridge in the room until each acknowledges over RPC. State
  is re-pushed only when it changes or a peer rejoins, so steady-state traffic is
  zero. A peer that keeps failing (e.g. a non-bridge participant with no handler)
  is dropped after a few attempts and retried only when new state arrives.
- **Inbound** (`in` / `bidirectional`): the bridge republishes received messages
  on a `TRANSIENT_LOCAL` publisher, so ROS subscribers that start after the
  bridge (RViz, Foxglove, tf listeners) still receive the latched state. The ROS
  message type travels in the RPC payload, so the bridge creates the
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

On the consuming bridge:

```yaml
topics:
  - topic: "/tf_static"
    direction: "in"
    latched: true
```

## Video Options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `bitrate_kbps` | integer | no | Target bitrate in kbps. Must be positive. |
| `codec` | string | no | Video codec name. Must be non-empty when set. |

Audio options are not part of config version `0.0.1`.

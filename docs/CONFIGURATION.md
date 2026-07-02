# Configuration Guide
The bridge reads the YAML config once on node startup and builds an immutable
snapshot of the user specified config. Pass the file path with the node's
`config_path` ROS parameter.

## Credentials

LiveKit credentials are not read from the config file. Set `LIVEKIT_URL` and
`LIVEKIT_TOKEN` in the node environment.


## Minimal Config

```yaml
ros2_livekit_bridge:
  version: "0.0.1"
  room_name: "robo_room"
```

The parser rejects unknown fields, empty required strings, invalid enum values,
and non-positive integer values where a positive value is required.

## Top-Level Fields

All config lives under `ros2_livekit_bridge`.

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `version` | string | yes | - | Must be `"0.0.1"`. |
| `room_name` | string | yes | - | LiveKit room name. |
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
| `topic` | string | yes | ROS topic pattern. Must be non-empty. |
| `direction` | string | yes | `in`, `out`, or `bidirectional`. |
| `preserve_id` | boolean | no | Default `false`. Inbound topics only. Prefix the republished ROS topic with the publishing participant's identity. |
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
ignored for outbound topics. It defaults to `false`, which republishes an
inbound data track under its own topic name (e.g. `/tf` stays `/tf`). When set
to `true`, the publishing participant's identity is prepended to the
republished ROS topic name, which prevents collisions when multiple
participants publish the same topic:

```yaml
topics:
  - topic: "/tf"
    direction: "in"
    preserve_id: true
```

```text
LiveKit data track: /tf   (from participant "robot-1")
ROS topic:          /robot_1/tf
```

The identity is sanitized into a legal ROS name token, so any character that is
not alphanumeric or `_` becomes `_` (e.g. `robot-1` → `robot_1`).

## Video Options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `bitrate_kbps` | integer | no | Target bitrate in kbps. Must be positive. |
| `codec` | string | no | Video codec name. Must be non-empty when set. |

Audio options are not part of config version `0.0.1`.

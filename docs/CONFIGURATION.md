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
| `direction` | string | yes | `in` or `out`. |
| `participant` | string | yes | LiveKit participant identity. Must be non-empty. |
| `msg_type` | string | yes | ROS service type, such as `std_srvs/srv/SetBool`. Must be non-empty. |

Service direction does not accept `bidirectional`. For service forwarding,
`direction: "out"` creates a local ROS service server that forwards calls to
the configured LiveKit participant using `msg_type`.

## Topics

| Field | Type | Required | Description |
|---|---:|---:|---|
| `topic` | string | yes | ROS topic pattern. Must be non-empty. |
| `direction` | string | yes | `in`, `out`, or `bidirectional`. |
| `video_options` | map | no | Optional video publish settings. |

Outgoing topics are those with `direction: "out"` or
`direction: "bidirectional"`. Incoming topics are those with `direction: "in"`
or `direction: "bidirectional"`. Topic patterns are ECMAScript regular
expressions matched against the full topic name.

## Video Options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `bitrate_kbps` | integer | no | Target bitrate in kbps. Must be positive. |
| `codec` | string | no | Video codec name. Must be non-empty when set. |

Audio options are not part of config version `0.0.1`.

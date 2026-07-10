# Bridge configuration

The bridge is configured with a YAML file passed via the `config_path` parameter
(see the launch files). The schema is
`ros2_livekit_bridge_config/schema/ros2_livekit_bridge_config.schema.json`, which
also generates the config parser at build time — edit the schema, not the
generated code.

## Top-level

| Field | Default | Description |
| --- | --- | --- |
| `version` | — | Schema version. Must be `"0.0.1"`. |
| `room_name` | — | LiveKit room the bridge joins. |
| `topic_polling_period_ms` | `500` | How often the bridge scans the ROS graph for newly matching topics. |
| `ros_threads` | `4` | MultiThreadedExecutor thread count (`0` = all cores). |
| `services` | `[]` | Outbound ROS service routes forwarded over LiveKit RPC. |
| `topics` | `[]` | Topic forwarding rules (below). |

## Topic rules (`topics[]`)

Each entry matches ROS topics and controls how they cross the bridge.

| Field | Applies to | Description |
| --- | --- | --- |
| `topic` | all | ROS topic name. Treated as a regex for the DataTrack path; matched as a literal name for `max_rate_hz` and `latched`. |
| `direction` | all | `out` (ROS → LiveKit), `in` (LiveKit → ROS), or `bidirectional`. |
| `msg_type` | inbound | Explicit ROS message type for a pure-inbound DataTrack whose type cannot be inferred locally (BOT-331 stopgap). Not needed for `latched` topics — their type travels in the RPC payload. |
| `preserve_id` | inbound | Prefix the republished ROS topic with the publishing participant's sanitized identity (e.g. `/tf` → `/robot_1/tf`). |
| `max_rate_hz` | outbound | Cap the outbound forward rate (Hz) via zero-order hold. |
| `latched` | out/in | Treat the topic as latched (see below). |
| `video_options` | outbound | Encoding options for image topics forwarded as video tracks. |

## Latched topics (`latched: true`)

Some ROS topics are **latched**: they are published once (or rarely) with
`RELIABLE` + `TRANSIENT_LOCAL` durability, and DDS replays the last sample to
subscribers that join later. `/tf_static` is the canonical example — static
transforms are broadcast once at startup, and a node that starts afterward still
receives them.

LiveKit DataTracks are **not** latched: a frame pushed before a peer subscribes
is lost, and a static topic never republishes it. Setting `latched: true` routes
the topic off the DataTrack path and onto a reliable **RPC push-with-ack**
mechanism instead:

- **Outbound** (`out`/`bidirectional`): the bridge subscribes with
  `TRANSIENT_LOCAL` QoS (so it captures state published before the bridge
  started), stores the topic's distinct messages, and a background worker pushes
  them to every peer bridge in the room until each acknowledges over RPC. State
  is re-pushed only when it changes or a peer rejoins, so steady-state traffic is
  zero. A peer that keeps failing (e.g. a non-bridge participant with no handler)
  is dropped after a few attempts and retried only when new state arrives.
- **Inbound** (`in`/`bidirectional`): the bridge republishes received messages on
  a `TRANSIENT_LOCAL` publisher, so ROS subscribers that start after the bridge
  (RViz, Foxglove, tf listeners) still receive the latched state. The ROS message
  type travels in the RPC payload, so `msg_type` is not required.

### Limits

The push uses a LiveKit RPC whose payload is capped at **15 KiB** (base64 leaves
~11 KiB of message bytes per call). A single message larger than that cannot be
sent and is skipped with a logged warning. This is ample for typical `/tf_static`
trees (a few hundred short-named transforms), but large monolithic latched topics
(e.g. `/robot_description`, large `/map`) are out of scope for this path — keep
them on the DataTrack path or handle them out of band.

### Example

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

# ROS2 CLI Manager

The `Ros2CliManager` ([src/ros2_cli_manager.cpp](../src/ros2_cli_manager.cpp))
lets you run a subset of the `ros2` CLI introspection commands against a
**remote** robot's ROS graph that is connected to the same LiveKit room. It is the bridge
component that makes commands like `ros2 topic list` work across the network.

## How it works

Each supported command is exposed two ways:

1. **As a local ROS service.** When the bridge node starts, the manager creates
   one ROS service per command (e.g. `/ros2_livekit_bridge/ros2_topic_list`).
   You call these like any normal ROS service with `ros2 service call`.
2. **As a LiveKit RPC method.** The same manager registers a matching LiveKit
   RPC handler (e.g. `ros2_topic_list`) on the room's local participant.

The two halves connect across the network:

- When you call the **local ROS service**, the manager validates the request,
  serializes it to JSON, and performs a **LiveKit RPC** to the
  `participant_id` named in the request. The remote bridge's RPC handler runs
  the actual ROS graph query locally and returns the formatted text.
- The result is returned to you in the service response's `output` field, with
  `success` / `err_msg` indicating whether the RPC round-trip succeeded.

```
ros2 service call  ──>  ROS2 LiveKit Bridge  ──> LiveKit RPC  ──>  remote ROS2 LiveKit Bridge  ──>  remote ROS graph
   (your machine)        (local bridge)                         (robot bridge)            (introspection)
```

Every request takes a `participant_id` (the LiveKit identity of the remote
bridge to query) and a `timeout_sec`. A `timeout_sec` of `0` means "use the
default", which is **10 seconds**. If the participant is unknown or
`participant_id` is empty, the call fails fast with an error message before any
RPC is attempted.

For `ros2 service call` only, the LiveKit RPC timeout is the requested
service-call timeout plus a **1 second** margin. That lets the remote bridge
return service-level errors such as `Service call timed out.` before the RPC
layer aborts the round-trip.

The remaining request fields mirror the flags of the corresponding native
`ros2` CLI command 1:1, so output matches what you'd see running the command
directly on the robot.

---
## `ros2 topic`
### `ros2 topic list`

**ROS service:** `/ros2_livekit_bridge/ros2_topic_list`
(type `ros2_livekit_bridge_msgs/srv/Ros2TopicList`)
**LiveKit RPC method:** `ros2_topic_list`

Request fields (beyond `participant_id` / `timeout_sec`):

| Field | Maps to `ros2 topic list` flag |
| --- | --- |
| `show_types` | `-t` / `--show-types` |
| `count_topics` | `-c` / `--count-topics` |
| `include_hidden_topics` | `--include-hidden-topics` |
| `verbose` | `-v` / `--verbose` |

Sample calls:

```bash
# List topics on the remote participant "robot-01"
ros2 service call /ros2_livekit_bridge/ros2_topic_list \
  ros2_livekit_bridge_msgs/srv/Ros2TopicList \
  "{participant_id: 'robot-01'}"

# Include message types and hidden topics, with a 5s timeout
ros2 service call /ros2_livekit_bridge/ros2_topic_list \
  ros2_livekit_bridge_msgs/srv/Ros2TopicList \
  "{participant_id: 'robot-01', timeout_sec: 5, show_types: true, include_hidden_topics: true}"

# Just count the topics
ros2 service call /ros2_livekit_bridge/ros2_topic_list \
  ros2_livekit_bridge_msgs/srv/Ros2TopicList \
  "{participant_id: 'robot-01', count_topics: true}"
```

### `ros2 topic pub`

**ROS service:** `/ros2_livekit_bridge/ros2_topic_pub`
(type `ros2_livekit_bridge_msgs/srv/Ros2TopicPub`)
**LiveKit RPC method:** `ros2_topic_pub`

Request fields (beyond `participant_id` / `timeout_sec`):

| Field | Maps to `ros2 topic pub` argument |
| --- | --- |
| `topic` | topic name, such as `/cmd_vel`; relative names resolve in the remote bridge node context |
| `interface_type` | message type, such as `geometry_msgs/msg/Twist` |
| `payload` | native YAML message payload, such as `{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}` |

This command accepts the same YAML-style message payload that native
`ros2 topic pub` accepts. The LiveKit RPC payload carries the YAML string to the
remote bridge, where it is converted into the requested ROS message type and
published:

```json
{
  "topic": "/cmd_vel",
  "interface_type": "geometry_msgs/msg/Twist",
  "payload": "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}",
  "timeout_sec": 10
}
```

The remote bridge resolves the topic name, checks it against configured
LiveKit-to-ROS topic rules (`in` or `bidirectional`), verifies the requested
type against the ROS graph when the topic is already known, then publishes the
serialized message with a cached generic publisher.

Sample call:

```bash
ros2 service call /ros2_livekit_bridge/ros2_topic_pub \
  ros2_livekit_bridge_msgs/srv/Ros2TopicPub \
  "{participant_id: 'robot-01', topic: '/test/cmd_vel', interface_type: 'geometry_msgs/msg/Twist', payload: '{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}'}"
```

---
## `ros2 service`
### `ros2 service list`

**ROS service:** `/ros2_livekit_bridge/ros2_service_list`
(type `ros2_livekit_bridge_msgs/srv/Ros2ServiceList`)
**LiveKit RPC method:** `ros2_service_list`

Request fields (beyond `participant_id` / `timeout_sec`):

| Field | Maps to `ros2 service list` flag |
| --- | --- |
| `show_types` | `-t` / `--show-types` |
| `count_services` | `-c` / `--count-services` |
| `include_hidden_services` | `--include-hidden-services` |

Sample calls:

```bash
# List services on the remote participant "robot-01"
ros2 service call /ros2_livekit_bridge/ros2_service_list \
  ros2_livekit_bridge_msgs/srv/Ros2ServiceList \
  "{participant_id: 'robot-01'}"

# Show service types
ros2 service call /ros2_livekit_bridge/ros2_service_list \
  ros2_livekit_bridge_msgs/srv/Ros2ServiceList \
  "{participant_id: 'robot-01', show_types: true}"

# Count services, including hidden ones
ros2 service call /ros2_livekit_bridge/ros2_service_list \
  ros2_livekit_bridge_msgs/srv/Ros2ServiceList \
  "{participant_id: 'robot-01', count_services: true, include_hidden_services: true}"
```

### `ros2 service call`

**ROS service:** `/ros2_livekit_bridge/ros2_service_call`
(type `ros2_livekit_bridge_msgs/srv/Ros2ServiceCall`)
**LiveKit RPC method:** `ros2_service_call`

Request fields (beyond `participant_id` / `timeout_sec`):

| Field | Maps to `ros2 service call` argument |
| --- | --- |
| `service` | service name, such as `/set_bool`; relative names resolve in the remote bridge node context |
| `interface_type` | required service type, such as `std_srvs/srv/SetBool` |
| `payload` | native YAML request payload, such as `{data: true}` |

The local bridge validates the YAML request using `interface_type` and sends the
request over LiveKit RPC. The JSON payload carries the requested
`timeout_sec` for the remote ROS service wait; the LiveKit RPC itself waits one
second longer so timeout responses can propagate back to the caller.

Sample calls

```bash
# Call a remote SetBool service on participant "robot-01"
ros2 service call /ros2_livekit_bridge/ros2_service_call \
  ros2_livekit_bridge_msgs/srv/Ros2ServiceCall \
  "{participant_id: 'robot-01', timeout_sec: 0, service: '/set_bool', interface_type: 'std_srvs/srv/SetBool', payload: '{data: true}'}"
```

The response `output` field contains CLI-friendly YAML text rendered from the
remote service response.

---

## `ros2 interface`
- currently only supports interface show
### `ros2 interface show`

**ROS service:** `/ros2_livekit_bridge/ros2_interface_show`
(type `ros2_livekit_bridge_msgs/srv/Ros2InterfaceShow`)
**LiveKit RPC method:** `ros2_interface_show`

Request fields (beyond `participant_id` / `timeout_sec`):

| Field | Maps to `ros2 interface show` flag |
| --- | --- |
| `type` | the interface type to show (e.g. `std_msgs/msg/String`) |
| `all_comments` | `--all-comments` |
| `no_comments` | `--no-comments` |

`all_comments` and `no_comments` are mutually exclusive; setting both fails with
an error before the RPC is performed.

Sample calls:

```bash
# Show a message definition from the remote participant "robot-01"
ros2 service call /ros2_livekit_bridge/ros2_interface_show \
  ros2_livekit_bridge_msgs/srv/Ros2InterfaceShow \
  "{participant_id: 'robot-01', type: 'std_msgs/msg/String'}"

# Show a service definition with all comments
ros2 service call /ros2_livekit_bridge/ros2_interface_show \
  ros2_livekit_bridge_msgs/srv/Ros2InterfaceShow \
  "{participant_id: 'robot-01', type: 'example_interfaces/srv/AddTwoInts', all_comments: true}"

# Show a definition with comments stripped
ros2 service call /ros2_livekit_bridge/ros2_interface_show \
  ros2_livekit_bridge_msgs/srv/Ros2InterfaceShow \
  "{participant_id: 'robot-01', type: 'sensor_msgs/msg/Image', no_comments: true}"
```

---

## Response format

All services share the same response shape:

| Field | Description |
| --- | --- |
| `success` | `true` if the remote query and RPC round-trip succeeded |
| `err_msg` | Human-readable error when `success` is `false` |
| `output` | The formatted CLI text, identical to native `ros2` output |

Common failure cases reported through `err_msg`:

- `participant_id must be non-empty`
- `LiveKit participant '<id>' was not found`
- `all_comments and no_comments are mutually exclusive` (interface show only)
- `interface_type must be non-empty` (service call and topic pub)
- `Service call timed out.` (service call only)
- LiveKit RPC errors (e.g. timeout, remote handler failure)
- malformed JSON returned by the remote bridge

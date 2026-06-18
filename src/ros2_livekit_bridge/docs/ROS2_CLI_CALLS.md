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
ros2 service call  ──>  Ros2CliManager  ──> LiveKit RPC  ──>  remote Ros2CliManager  ──>  remote ROS graph
   (your machine)        (local bridge)                         (robot bridge)            (introspection)
```

Every request takes a `participant_id` (the LiveKit identity of the remote
bridge to query) and a `timeout_sec`. A `timeout_sec` of `0` means "use the
default", which is **10 seconds**. If the participant is unknown or
`participant_id` is empty, the call fails fast with an error message before any
RPC is attempted.

The remaining request fields mirror the flags of the corresponding native
`ros2` CLI command 1:1, so output matches what you'd see running the command
directly on the robot.

---

## `ros2 topic list`

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

---

## `ros2 service list`

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

---

## `ros2 interface show`

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

All three services share the same response shape:

| Field | Description |
| --- | --- |
| `success` | `true` if the remote query and RPC round-trip succeeded |
| `err_msg` | Human-readable error when `success` is `false` |
| `output` | The formatted CLI text, identical to native `ros2` output |

Common failure cases reported through `err_msg`:

- `participant_id must be non-empty`
- `LiveKit participant '<id>' was not found`
- `all_comments and no_comments are mutually exclusive` (interface show only)
- LiveKit RPC errors (e.g. timeout, remote handler failure)
- malformed JSON returned by the remote bridge

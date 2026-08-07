# Diagnostics

`ros_portal` publishes ROS diagnostics on `/diagnostics` using
`diagnostic_updater` from the [ROS diagnostics](https://github.com/ros/diagnostics)
stack. ROS Portal currently exposes five diagnostic tasks:

- `build_info` — always published
- `ros_portal_status` — always published
- `connection_health` — published after configuration loads
- `topic_forwarder` — published when topic forwarding is configured
- `cli_manager` — published when the ROS CLI manager is enabled

ROS Portal publishes raw `diagnostic_msgs/msg/DiagnosticArray` messages. Grouped
`/diagnostics_agg` output is provided by a separate `diagnostic_aggregator`
configuration, not by ROS Portal node itself.

## Publication

Diagnostics are published periodically, not only when connection events occur.
`diagnostic_updater` publishes diagnostics on its own timer (configured via the
`diagnostic_updater.period` ROS parameter, defaulting to 1 Hz). Separately, the
ROS Portal polls LiveKit RTC stats from an internal 1 Hz wall timer while connected.

LiveKit connection callbacks update cached state only. They do not publish
diagnostics directly from SDK callback threads.

## `build_info`

Reports the versions ROS Portal was built with. All values are fixed at build
time (or read once from the environment at startup) and never change while the
process runs, so this task always reports `OK` and exists purely to surface
version information at the top of the diagnostics tree.

| Property | Value |
|---|---|
| Task name | `build_info` |
| Topic | `/diagnostics` |
| Hardware ID | `ros_portal` |
| Source | Compile-time constants baked in by CMake, plus the `ROS_DISTRO` environment variable |

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `LiveKit SDK <version>` | Always. The message carries the LiveKit C++ SDK version so it is visible without expanding key/value fields. |

### Key/Value Fields

| Key | Value |
|---|---|
| `livekit_sdk_version` | LiveKit C++ SDK version ROS Portal was built against, as exported by the SDK package found by CMake (falls back to the requested `LIVEKIT_SDK_VERSION` pin, then `unknown`). |
| `ros_portal_version` | `ros_portal` package version from `package.xml`, or `unknown`. |
| `ros_distro` | ROS distribution from the `ROS_DISTRO` environment variable at startup, or `unknown`. |

The reported SDK version is resolved at build time; the prebuilt SDK exposes no
runtime version API, so a mismatched library swapped in after the build cannot
be detected here.

Both versions come from `ros_portal/version.hpp`, which CMake renders from
`src/ros_portal/cmake/version.hpp.in` during the build. The header is installed
with the package, so downstream code can read `ROS_PORTAL_VERSION`,
`ROS_PORTAL_LIVEKIT_SDK_VERSION`, and the `ROS_PORTAL_VERSION_AT_LEAST()` guard
directly instead of going through diagnostics.

## `ros_portal_status`

Reports the node's initialization lifecycle, effective
configuration, component health, shared LiveKit RPC failures, and topic polling
health. The task exists as soon as the node is constructed, so configuration and
credential failures remain observable.

| Property | Value |
|---|---|
| Task name | `ros_portal_status` |
| Topic | `/diagnostics` |
| Hardware ID | `ros_portal` |
| Source | Node lifecycle, effective configuration, component ownership, shared RPC helpers, and topic polling timer |

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `ROS Portal is initialized` | Initialization completed and the topic polling timer is active. |
| `WARN` | `ROS Portal topic polling has overrun` | At least one discovery poll exceeded the configured polling period. |
| `ERROR` | `ROS Portal is not initialized` | Initialization has not completed, including configuration, credential, or connection failures. |
| `ERROR` | `ROS Portal is initialized without an active topic poll timer` | Lifecycle state says initialized but topic discovery polling is inactive. |
| `ERROR` | `ROS Portal has inactive components` | At least one expected component is not active. |

### Fields

| Key | Value |
|---|---|
| `initialized` | Whether initialization completed. |
| `components_inactive` | Comma-separated inactive component names, or `none`. Components are `connection_manager`, `topic_forwarder`, `latched_topic_forwarder`, `service_forwarder`, and `cli_manager`. Any inactive component triggers an `ERROR` status. |
| `config_path` | Effective configuration file path, or `unset`. |
| `topic_polling_period_ms` | Effective topic discovery polling period. |
| `local_identity` | Connected local participant identity, or `unset`. |
| `rpc_register_failures` | Cumulative failures from the shared RPC registration helper. |
| `rpc_perform_failures` | Cumulative failures from the shared outbound RPC helper. |
| `topic_poll_overruns` | Cumulative topic polls that exceeded `topic_polling_period_ms`. |

## `connection_health`

Reports whether ROS Portal is connected to its LiveKit room and includes a
small RTC transport and traffic summary.

| Property | Value |
|---|---|
| Task name | `connection_health` |
| Topic | `/diagnostics` |
| Hardware ID | `ros_portal` |
| Source | LiveKit room connection state, participant updates, and `Room::getStats()` |

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `Connected to LiveKit room` | The LiveKit SDK reports the room connection as connected. |
| `WARN` | `Reconnecting to LiveKit room` | The LiveKit SDK is attempting to restore an interrupted connection. |
| `ERROR` | `Disconnected from LiveKit room` | ROS Portal is not connected to the LiveKit room. |

The `state` key/value field is the source of this status level: `connected`
maps to `OK`, `reconnecting` maps to `WARN`, and `disconnected` maps to
`ERROR`.

With valid credentials, ROS Portal remains alive while the LiveKit server is
unavailable and retries at 1 Hz. During that interval it publishes the
`disconnected` state. Missing credentials or invalid local configuration still
prevent node initialization.

### Base Key/Value Fields

Every `connection_health` status includes these fields:

| Key | Value |
|---|---|
| `state` | `connected`, `reconnecting`, or `disconnected`. |
| `num_peers` | Current number of known remote LiveKit participants. |
| `reconnect_count` | Number of times the LiveKit SDK entered an in-session reconnecting state after an established connection dropped. A terminal disconnect followed by a new `Room::connect` does not increment this counter. |
| `connection_loss_count` | Number of transitions from connected to unavailable, including both SDK reconnects and direct terminal disconnects. |
| `room_name` | LiveKit room name from the active room connection. |

### RTC Summary Fields

When connected, the status also includes a fixed set of RTC summary fields. RTC
fields are omitted while disconnected or reconnecting.

| Key | Value |
|---|---|
| `rtc.stats_available` | `true` when a LiveKit stats snapshot has been summarized, otherwise `false`. |
| `rtc.transport.ice_state` | Selected transport ICE state, or `unset`. |
| `rtc.transport.dtls_state` | Selected transport DTLS state, or `unset`. |
| `rtc.transport.candidate_pair_state` | Selected ICE candidate pair state, or `unset`. |
| `rtc.transport.current_round_trip_time_ms` | Selected candidate pair RTT in milliseconds, or `unset`. |
| `rtc.transport.available_outgoing_bitrate_bps` | Available outgoing bitrate reported by the selected candidate pair, or `unset`. |
| `rtc.transport.available_incoming_bitrate_bps` | Available incoming bitrate reported by the selected candidate pair, or `unset`. |
| `rtc.traffic.bytes_sent` | Selected candidate pair bytes sent, or `unset`. |
| `rtc.traffic.bytes_received` | Selected candidate pair bytes received, or `unset`. |
| `rtc.traffic.send_bitrate_bps` | Send bitrate computed from consecutive selected candidate pair byte counters, or `unset`. |
| `rtc.traffic.receive_bitrate_bps` | Receive bitrate computed from consecutive selected candidate pair byte counters, or `unset`. |
| `rtc.traffic.packets_lost` | Sum of packet loss from local inbound RTP stats and remote inbound RTP stats. |
| `rtc.traffic.max_jitter_ms` | Maximum jitter from local inbound RTP stats and remote inbound RTP stats, in milliseconds, or `unset`. |
| `rtc.data_channels.open` | Number of open WebRTC data channels. |
| `rtc.data_channels.total` | Total number of WebRTC data channels. |

The selected candidate pair is the transport's selected candidate pair when it
can be matched. If not, ROS Portal uses the nominated, succeeded candidate pair
with the most observed traffic.

ROS Portal intentionally does not publish certificate material, codec details,
candidate IDs, per-stream IDs, per-track stats, or the full WebRTC stats tree on
`/diagnostics`.

Example fields:

```text
rtc.stats_available=true
rtc.transport.ice_state=completed
rtc.transport.dtls_state=connected
rtc.transport.candidate_pair_state=succeeded
rtc.transport.current_round_trip_time_ms=12
rtc.traffic.bytes_sent=1048576
rtc.traffic.bytes_received=524288
rtc.traffic.send_bitrate_bps=820000
rtc.traffic.receive_bitrate_bps=410000
rtc.traffic.packets_lost=0
rtc.data_channels.open=1
rtc.data_channels.total=1
```

## `topic_forwarder`

Reports whether inbound DataTrack schemas match the ROS topic types ROS Portal
expects.

| Property | Value |
|---|---|
| Task name | `topic_forwarder` |
| Topic | `/diagnostics` |
| Hardware ID | `ros_portal` |
| Source | Inbound DataTrack schema validation in `TopicForwarder` |

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `Inbound schemas valid` | No inbound schema mismatches have been observed. |
| `WARN` | `Inbound schema validation failures detected` | At least one inbound DataTrack schema did not match the expected ROS type. |

### Key/Value Fields

| Key | Value |
|---|---|
| `inbound_schemas_incorrect` | Cumulative count of inbound schema validation failures. |

## `cli_manager`

Reports whether ROS Portal's ROS CLI services and matching LiveKit RPC handlers
are registered, plus cache pressure and remote-call failure counters.

| Property | Value |
|---|---|
| Task name | `cli_manager` |
| Topic | `/diagnostics` |
| Hardware ID | `ros_portal` |
| Source | CLI ROS service and LiveKit RPC registration, publisher/client caches, and remote RPC outcomes |

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `All CLI command pairs registered` | Every CLI ROS service has a matching LiveKit RPC handler. |
| `WARN` | `CLI command pairs registered; publisher/client cache is dropping requests` | Registration is complete, but a bounded cache rejected new topic publishers or service clients because it was full. |
| `ERROR` | `One or more CLI command pairs failed to register` | At least one CLI ROS service or LiveKit RPC handler is missing. |

### Registration Fields

Each CLI command pair is reported under its RPC method name:

| Key | Value |
|---|---|
| `ros2_topic_list` | `ok`, or `service missing`, `rpc missing`, or `service and rpc missing`. |
| `ros2_topic_pub` | Same as above. |
| `ros2_service_list` | Same as above. |
| `ros2_service_call` | Same as above. |
| `ros2_interface_show` | Same as above. |

### Cache and Remote-Call Fields

| Key | Value |
|---|---|
| `topic_pub_cache` | Current and maximum cached generic topic publishers, for example `0/20`. |
| `topic_pub_cache_full_rejections` | Number of topic publish requests rejected because the cache was full. |
| `service_call_cache` | Current and maximum cached service clients, for example `0/20`. |
| `service_call_cache_full_rejections` | Number of service call requests rejected because the cache was full. |
| `remote_participant_not_found` | Cumulative remote RPC failures because the target participant was absent. Informational; does not change the diagnostic level by itself. |
| `remote_transport_failures` | Cumulative remote RPC transport failures. Informational; does not change the diagnostic level by itself. |
| `remote_malformed_responses` | Cumulative remote RPC responses that could not be parsed. Informational; does not change the diagnostic level by itself. |

## Quick Check

After launching ROS Portal with valid LiveKit credentials, inspect one diagnostic
message with:

    ros2 topic echo /diagnostics

Look for `build_info`, `ros_portal_status`, and, after configuration loads,
`connection_health`. When those subsystems are enabled, look for
`topic_forwarder` and `cli_manager` statuses. When the room is connected,
`connection_health` should also contain the fixed `rtc.*` summary fields.

## Aggregating Diagnostics

ROS Portal publishes raw diagnostics only. To expose grouped health on
`/diagnostics_agg` and `/diagnostics_toplevel_state`, run the standard ROS
[`diagnostic_aggregator`](https://github.com/ros/diagnostics) node alongside
ROS Portal.

### Required Dependencies

ROS Portal package depends on:

- `diagnostic_msgs`
- `diagnostic_updater`

These are declared by `ros_portal` and are required for publishing
`/diagnostics`.

Aggregated output additionally requires `diagnostic_aggregator`. Install the ROS
diagnostics packages for your ROS distribution:

```bash
sudo apt install ros-${ROS_DISTRO}-diagnostic-aggregator
```

For source-based workspaces, make sure the diagnostics repositories from
`external.repos` are imported and installable with rosdep:

```bash
mkdir -p src/externals
vcs import --skip-existing src/externals < external.repos
./scripts/apply-external-patches.sh
rosdep install --from-paths src --ignore-src -r -y
```

### Sample Aggregator Config

Save this as `ros_portal_diagnostics_aggregator.yaml`:

```yaml
/**:
  ros__parameters:
    path: ROS Portal
    build_info:
      type: diagnostic_aggregator/GenericAnalyzer
      path: Build Info
      contains: ['build_info']
    ros_portal_status:
      type: diagnostic_aggregator/GenericAnalyzer
      path: Node Status
      contains: ['ros_portal_status']
    connection_health:
      type: diagnostic_aggregator/GenericAnalyzer
      path: Connection Health
      contains: ['connection_health']
    topic_forwarder:
      type: diagnostic_aggregator/GenericAnalyzer
      path: Topic Forwarder
      contains: ['topic_forwarder']
    cli_manager:
      type: diagnostic_aggregator/GenericAnalyzer
      path: CLI Manager
      contains: ['cli_manager']
```

This groups ROS Portal diagnostics under `ROS Portal / Build Info`,
`ROS Portal / Node Status`, `ROS Portal / Connection Health`,
`ROS Portal / Topic Forwarder`, and `ROS Portal / CLI Manager`.
Omit analyzers for tasks that are not enabled in your configuration.

### Launch The Aggregator

Start ROS Portal normally, then run:

    ros2 run diagnostic_aggregator aggregator_node \
    --ros-args --params-file ros_portal_diagnostics_aggregator.yaml

or put the following in your launch file:
```xml
<node pkg="diagnostic_aggregator" exec="aggregator_node" name="diagnostic_aggregator">
  <param name="config_file" value="$(find ros_portal)/config/ros_portal_diagnostics_aggregator.yaml"/>
</node>
```

Inspect the grouped aggregated diagnostics:

    ros2 topic echo /diagnostics_agg --once

Inspect the top-level state:

    ros2 topic echo /diagnostics_toplevel_state --once

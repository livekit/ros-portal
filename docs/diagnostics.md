# Diagnostics

`ros_portal` publishes ROS diagnostics on `/diagnostics` using
`diagnostic_updater` from the [ROS diagnostics](https://github.com/ros/diagnostics)
stack. ROS Portal currently exposes seven diagnostic tasks:

- `build_info` — always published
- `ros_portal_status` — always published
- `connection_health` — published after configuration loads
- `topic_forwarder` — published when topic forwarding is configured
- `latched_topic_forwarder` — published when latched topic forwarding is configured
- `service_forwarder` — published when service forwarding is configured
- `cli_manager` — published when the ROS CLI manager is enabled

Every task publishes on the `/diagnostics` topic with hardware ID `ros_portal`,
and each task name is the section heading it is documented under below. Per-task
sections therefore list only what differs between them: the source of the data,
the status levels, and the key/value fields.

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

**Source:** Compile-time constants baked in by CMake, plus the `ROS_DISTRO` environment variable

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `LiveKit SDK <version>` | Always. The message carries the LiveKit C++ SDK version so it is visible without expanding key/value fields. |

### Key/Value Fields

| Key | Value |
|---|---|
| `livekit_sdk_version` | LiveKit C++ SDK version ROS Portal was built against, as exported by the SDK package found by CMake (falls back to the requested `LIVEKIT_SDK_VERSION` pin, then `unknown`). |
| `ros_distro` | ROS distribution from the `ROS_DISTRO` environment variable at startup, or `unknown`. |
| `ros_portal_version` | `ros_portal` package version from `package.xml`, or `unknown`. |

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
configuration, component health, and graph discovery health. The task exists as
soon as the node is constructed, so configuration and credential failures remain
observable.

**Source:** Node lifecycle, effective configuration, component ownership, and the graph discovery worker

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `ROS Portal is initialized` | Initialization completed and the graph discovery worker is running. |
| `ERROR` | `ROS Portal is not initialized` | Initialization has not completed, including configuration, credential, or connection failures. |
| `ERROR` | `ROS Portal is initialized without an active graph discovery worker` | Lifecycle state says initialized but the graph-event discovery worker is not running. |
| `ERROR` | `ROS Portal has inactive components` | At least one expected component is not active. `latched_topic_forwarder` is expected only when at least one latched topic is configured. |

### Fields

| Key | Value |
|---|---|
| `components_inactive` | Comma-separated inactive component names, or `none`. Components are `connection_manager`, `topic_forwarder`, `service_forwarder`, and `cli_manager`. `latched_topic_forwarder` is included only when at least one latched topic is configured. Any listed inactive component triggers an `ERROR` status. |
| `config_path` | Effective configuration file path, or `unset`. |
| `graph_discovery_active` | Whether the graph-event discovery worker is running. |
| `initialized` | Whether initialization completed. |
| `local_identity` | Connected local participant identity, or `default`. |
| `forwarding_paused` | Whether an operator paused forwarding operations with the `~/pause` service. |

## `connection_health`

Reports whether ROS Portal is connected to its LiveKit room and includes a
small RTC transport and traffic summary.

**Source:** LiveKit room connection state, participant updates, and `Room::getStats()`

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
| `connection_loss_count` | Number of transitions from connected to unavailable, including both SDK reconnects and direct terminal disconnects. |
| `num_peers` | Current number of known remote LiveKit participants. |
| `reconnect_count` | Number of times the LiveKit SDK entered an in-session reconnecting state after an established connection dropped. A terminal disconnect followed by a new `Room::connect` does not increment this counter. |
| `room_name` | LiveKit room name from the active room connection. |
| `state` | `connected`, `reconnecting`, or `disconnected`. |

### RTC Summary Fields

When connected, the status also includes a fixed set of RTC summary fields. RTC
fields are omitted while disconnected or reconnecting.

| Key | Value |
|---|---|
| `rtc.data_channels.open` | Number of open WebRTC data channels. |
| `rtc.data_channels.total` | Total number of WebRTC data channels. |
| `rtc.stats_available` | `true` when a LiveKit stats snapshot has been summarized, otherwise `false`. |
| `rtc.traffic.bytes_received` | Selected candidate pair bytes received, or `unset`. |
| `rtc.traffic.bytes_sent` | Selected candidate pair bytes sent, or `unset`. |
| `rtc.traffic.max_jitter_ms` | Maximum jitter from local inbound RTP stats and remote inbound RTP stats, in milliseconds, or `unset`. |
| `rtc.traffic.packets_lost` | Sum of packet loss from local inbound RTP stats and remote inbound RTP stats. |
| `rtc.traffic.receive_bitrate_bps` | Receive bitrate computed from consecutive selected candidate pair byte counters, or `unset`. |
| `rtc.traffic.send_bitrate_bps` | Send bitrate computed from consecutive selected candidate pair byte counters, or `unset`. |
| `rtc.transport.available_incoming_bitrate_bps` | Available incoming bitrate reported by the selected candidate pair, or `unset`. |
| `rtc.transport.available_outgoing_bitrate_bps` | Available outgoing bitrate reported by the selected candidate pair, or `unset`. |
| `rtc.transport.candidate_pair_state` | Selected ICE candidate pair state, or `unset`. |
| `rtc.transport.current_round_trip_time_ms` | Selected candidate pair RTT in milliseconds, or `unset`. |
| `rtc.transport.dtls_state` | Selected transport DTLS state, or `unset`. |
| `rtc.transport.ice_state` | Selected transport ICE state, or `unset`. |

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

Reports outbound and inbound track inventory and cumulative forwarding failures,
and raises an error when an outbound LiveKit writer is still pending or an
inbound reader thread has stopped.

**Source:** Outbound ROS subscriptions and LiveKit tracks, inbound DataTrack readers, and forwarding outcomes

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `Topic forwarding healthy` | All currently tracked forwarding paths are available and no failures have been observed. |
| `WARN` | `Topic forwarding failures or drops detected` | At least one cumulative forwarding failure, rejection, or drop counter is nonzero. |
| `ERROR` | `One or more forwarding paths are unavailable` | An outbound data track is pending its writer, or an active inbound track's reader thread has stopped. |

### Key/Value Fields

| Key | Value |
|---|---|
| `inbound.data_tracks` | Number of inbound LiveKit data tracks being republished. |
| `inbound.failures` | Cumulative inbound failures, aggregating tracks rejected because no ROS type could be resolved, because they matched no topic pattern, or because a ROS topic name could not be produced, plus failures publishing inbound frames on ROS, invalid inbound JSON frames, empty inbound CDR payloads, streams that ended with a terminal error, and inbound schema validation rejections. |
| `outbound.data_tracks` | Number of discovered outbound data topics. |
| `outbound.failures` | Cumulative outbound failures, aggregating LiveKit data-frame push failures, ROS-to-JSON conversion failures, ROS subscription creation failures, and outbound schema define, render, and encoding-mismatch failures. |
| `outbound.subscriptions` | Number of active outbound ROS subscriptions. |
| `outbound.video_tracks` | Number of discovered outbound image topics. |

`outbound.failures` and `inbound.failures` are deliberately coarse. Every
increment is logged individually with its specific cause, including the track,
topic, and participant involved, so the log is the place to identify which
failure occurred. A stream that ends with a terminal error logs that error text,
and schema rejections additionally log both the remote and local schema SHA-256
hashes.

The counts of outbound tracks awaiting a LiveKit writer or video sink and of
running inbound reader threads are not published as fields. They only raise the
summary to the `ERROR` level described above.

## `latched_topic_forwarder`

Reports latched-topic RPC registration, per-peer delivery state, and cumulative
outbound and inbound failures.

**Source:** Latched ROS subscriptions and publishers, stored outbound state, participant delivery bookkeeping, and RPC outcomes

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `Latched topic forwarding healthy` | Required RPC registration is available, configured outbound topics are subscribed, peers are current, and no failures have been observed. |
| `WARN` | `Latched topic forwarding is degraded` | An outbound topic is undiscovered, retained-message storage is full, a peer is behind, or a cumulative failure counter is nonzero. |
| `ERROR` | `Latched topic forwarding has an unavailable RPC path or peer` | Required inbound RPC registration failed or at least one peer reached the consecutive-failure cap. |

### Key/Value Fields

| Key | Value |
|---|---|
| `inbound.failures` | Cumulative inbound latched-state request failures, aggregating malformed payloads, requests for topics not configured inbound, payloads that were not valid base64, and requests that could not create or use the required ROS publisher. |
| `outbound.failures` | Cumulative outbound failures, aggregating messages dropped for exceeding the LiveKit RPC payload limit and failed outbound latched-state RPC calls. |
| `peers.behind` | Peers that have not acknowledged the current version and remain eligible for retries. |
| `peers.given_up` | Peers that reached the consecutive-failure cap. |
| `peers.total` | Number of remote participants in delivery bookkeeping. |
| `rpc_registered` | Whether the inbound latched-state RPC handler is registered. This is `false` when no inbound latched topics are configured because no handler is required. |

`outbound.failures` and `inbound.failures` are deliberately coarse. Every
increment is logged individually with its specific cause, including the topic or
participant involved, so the log is the place to identify which failure occurred.

### Inventory Logging

Latched-topic inventory is logged rather than published as diagnostic fields.
On startup the forwarder logs the number of configured outbound topics, the
number of configured inbound topics, and the retained-message capacity. Each
subsequent item is logged as it appears:

| Event | Log |
|---|---|
| An outbound latched topic is discovered and subscribed | `Subscribed to latched topic '<topic>' [<type>] (RELIABLE, TRANSIENT_LOCAL)` |
| A distinct outbound message is retained | `Stored latched message for '<topic>' (<n> retained, version <v>)` |
| An inbound TRANSIENT_LOCAL publisher is created | `Created TRANSIENT_LOCAL publisher for latched '<topic>' [<type>]` |
| An inbound request is republished on ROS | `Republished latched '<topic>' [<type>] (<n> bytes)` |

Because every inbound request either republishes or is rejected, and both are
logged, the log also accounts for the total inbound request count.

Undiscovered outbound topics and full retained-message storage are not published
as fields. They only raise the summary to `WARN` as described above.

## `service_forwarder`

Reports configured route availability and cumulative local-to-remote service
forwarding outcomes.

**Source:** Configured routes, runtime type support, LiveKit RPC outcomes, and local ROS response handling

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `Service forwarding healthy` | Every configured route was created and no request failure has been observed. |
| `WARN` | `Service forwarding failures detected` | A request-path failure, a response-path failure, or a handler exception occurred. |
| `ERROR` | `One or more configured service routes are unavailable` | The number of created services does not match the configured route count. |

### Key/Value Fields

| Key | Value |
|---|---|
| `handler_exceptions` | Exceptions caught while handling forwarded requests. |
| `last_failure_reason` | Stable category for the most recent failure, or `none`. |
| `request_failures` | Cumulative failures on the request path, aggregating requests rejected because the room was not connected, an absent target LiveKit participant, requests that could not be serialized for RPC, and failed LiveKit RPCs. |
| `requests_succeeded` | Cumulative requests whose remote response populated the local response. |
| `response_failures` | Cumulative failures on the response path, aggregating malformed RPC responses, errors returned by the remote service, remote responses that could not populate the local ROS response, and timeouts sending the response to the local ROS client. |
| `route_failures` | Cumulative route setup failures, aggregating routes skipped because service, type, or participant was empty, and routes skipped because runtime service type support could not be loaded. |
| `routes_configured` | Number of configured outgoing service routes. |
| `services_created` | Number of local forwarded ROS services successfully created. |

`route_failures`, `request_failures`, and `response_failures` are deliberately
coarse. Every increment is logged individually with its specific cause, including
the service, message type, and participant involved, so the log is the place to
identify which failure occurred. `last_failure_reason` additionally names the
category of the most recent one.

Every forwarded request lands in exactly one of `requests_succeeded`,
`request_failures`, `response_failures`, or `handler_exceptions`, so the four
counters partition request volume and no separate total is published.

## `cli_manager`

Reports whether ROS Portal's ROS CLI services and matching LiveKit RPC handlers
are registered, plus cache pressure and cumulative CLI RPC failures.

**Source:** CLI ROS service and LiveKit RPC registration, publisher/client caches, and remote RPC outcomes

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
| `ros2_interface_show` | `ok`, or `service missing`, `rpc missing`, or `service and rpc missing`. |
| `ros2_service_call` | Same as above. |
| `ros2_service_list` | Same as above. |
| `ros2_topic_list` | Same as above. |
| `ros2_topic_pub` | Same as above. |

### Cache Fields

| Key | Value |
|---|---|
| `service_call_cache_full_rejections` | Number of service call requests rejected because the cache was full. |
| `topic_pub_cache_full_rejections` | Number of topic publish requests rejected because the cache was full. |

Cache size and capacity are not published as fields. Each rejection logs the
cache utilization at the moment it filled, as
`Rejecting publish to '<topic>': generic publisher cache is full (<size>/<capacity>)`
and `Rejecting service call to '<service>': service client cache is full (<size>/<capacity>)`.

### RPC Outcome Fields

| Key | Value |
|---|---|
| `rpc_failures` | Cumulative CLI RPC failures, aggregating outbound remote calls that returned an unsuccessful response for any reason (an absent target participant, a LiveKit transport failure, a malformed response, or a remote error) and inbound CLI RPC requests that returned failure. Informational; does not change the diagnostic level by itself. |

`rpc_failures` is deliberately coarse and counts each failed request exactly
once, whichever cause produced it. Every increment is logged individually with
its RPC method and specific cause:

| Direction | Log |
|---|---|
| Outbound remote call failed | `Remote CLI RPC '<rpc_method>' failed: <reason>` |
| Inbound request failed | `Failed to handle LiveKit RPC '<rpc_method>': <reason>` |

Two inbound causes that previously had their own counters are logged separately
before the request fails: a publish rejected by the local allow policy logs
`Rejecting publish to '<topic>': not permitted by the local topic allow policy`,
and a local ROS service call that times out logs
`Local ROS service call for '<service>' timed out`.

Request volume is not published. The five CLI methods are
`ros2_topic_list`, `ros2_topic_pub`, `ros2_service_list`, `ros2_service_call`,
and `ros2_interface_show`.

## Quick Check

After launching ROS Portal with valid LiveKit credentials, inspect one diagnostic
message with:

    ros2 topic echo /diagnostics

Look for `build_info`, `ros_portal_status`, and, after configuration loads,
`connection_health`. When those subsystems are enabled, look for
`topic_forwarder`, `latched_topic_forwarder`, `service_forwarder`, and
`cli_manager` statuses. When the room is connected, `connection_health` should
also contain the fixed `rtc.*` summary fields.

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
    latched_topic_forwarder:
      type: diagnostic_aggregator/GenericAnalyzer
      path: Latched Topic Forwarder
      contains: ['latched_topic_forwarder']
    service_forwarder:
      type: diagnostic_aggregator/GenericAnalyzer
      path: Service Forwarder
      contains: ['service_forwarder']
    cli_manager:
      type: diagnostic_aggregator/GenericAnalyzer
      path: CLI Manager
      contains: ['cli_manager']
```

This groups ROS Portal diagnostics under `ROS Portal / Build Info`,
`ROS Portal / Node Status`, `ROS Portal / Connection Health`,
`ROS Portal / Topic Forwarder`, `ROS Portal / Latched Topic Forwarder`,
`ROS Portal / Service Forwarder`, and `ROS Portal / CLI Manager`.
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

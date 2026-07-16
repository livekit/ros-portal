# Diagnostics

`ros2_livekit_bridge` publishes ROS diagnostics on `/diagnostics` using
`diagnostic_updater`. The bridge currently exposes one diagnostic task:
`connection_health`.

The bridge publishes raw `diagnostic_msgs/msg/DiagnosticArray` messages. Grouped
`/diagnostics_agg` output is provided by a separate `diagnostic_aggregator`
configuration, not by the bridge node itself.

## Publication

Diagnostics are published periodically, not only when connection events occur.
`diagnostic_updater` publishes diagnostics on its own timer (configured via the
`diagnostic_updater.period` ROS parameter, defaulting to 1 Hz). Separately, the
bridge polls LiveKit RTC stats from an internal 1 Hz wall timer while connected.

LiveKit connection callbacks update cached state only. They do not publish
diagnostics directly from SDK callback threads.

## `connection_health`

Reports whether the bridge is connected to its LiveKit room and includes a
small RTC transport and traffic summary.

| Property | Value |
|---|---|
| Task name | `connection_health` |
| Topic | `/diagnostics` |
| Hardware ID | `ros2_livekit_bridge` |
| Source | LiveKit room connection state, participant updates, and `Room::getStats()` |

### Status Levels

| Level | Message | Meaning |
|---|---|---|
| `OK` | `Connected to LiveKit room` | The LiveKit SDK reports the room connection as connected. |
| `WARN` | `Reconnecting to LiveKit room` | The LiveKit SDK is attempting to restore an interrupted connection. |
| `ERROR` | `Disconnected from LiveKit room` | The bridge is not connected to the LiveKit room. |

If startup fails before the node remains alive, the bridge may exit before it can
publish an `ERROR` diagnostic.

### Base Key/Value Fields

Every `connection_health` status includes these fields:

| Key | Value |
|---|---|
| `connected` | `true` when connected, otherwise `false`. |
| `state` | `connected`, `reconnecting`, or `disconnected`. |
| `num_peers` | Current number of known remote LiveKit participants. |
| `reconnect_count` | Number of times the SDK has entered reconnecting state. |
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
can be matched. If not, the bridge uses the nominated, succeeded candidate pair
with the most observed traffic.

The bridge intentionally does not publish certificate material, codec details,
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

## Quick Check

After launching the bridge with valid LiveKit credentials, inspect one diagnostic
message with:

    ros2 topic echo /diagnostics --once

Look for a `connection_health` status containing the base fields above. When the
room is connected, the status should also contain the fixed `rtc.*` summary
fields.

## Aggregating Diagnostics

The bridge publishes raw diagnostics only. To expose grouped health on
`/diagnostics_agg` and `/diagnostics_toplevel_state`, run the standard ROS
`diagnostic_aggregator` node alongside the bridge.

### Required Dependencies

The bridge package depends on:

- `diagnostic_msgs`
- `diagnostic_updater`

These are declared by `ros2_livekit_bridge` and are required for publishing
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
rosdep install --from-paths src --ignore-src -r -y
```

### Sample Aggregator Config

Save this as `livekit_bridge_diagnostics_aggregator.yaml`:

```yaml
/**:
  ros__parameters:
    path: LiveKit Bridge
    connection_health:
      type: diagnostic_aggregator/GenericAnalyzer
      path: Connection Health
      contains: ['connection_health']
```

This groups the bridge's `connection_health` status under `LiveKit Bridge /
Connection Health`. Add more analyzers only when the bridge publishes additional
diagnostic tasks that should affect the aggregate health view.

### Launch The Aggregator

Start the bridge normally, then run:

    ros2 run diagnostic_aggregator aggregator_node \
    --ros-args --params-file livekit_bridge_diagnostics_aggregator.yaml

or put the following in your launch file:
```xml
<node pkg="diagnostic_aggregator" exec="aggregator_node" name="diagnostic_aggregator">
  <param name="config_file" value="$(find ros2_livekit_bridge)/config/livekit_bridge_diagnostics_aggregator.yaml"/>
</node>
```

Inspect the grouped aggregated diagnostics:

    ros2 topic echo /diagnostics_agg --once

Inspect the top-level state:

    ros2 topic echo /diagnostics_toplevel_state --once

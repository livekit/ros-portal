# LiveKit Diagnostics PoC

This package publishes simulated diagnostics for the ROS LiveKit bridge. It is
intended for demonstrating the shape of diagnostics data and grouped status
views without requiring a LiveKit room, real peers, or ROS graph activity.

## Running

Fetch the source dependencies pinned in `external.repos`, then build:

```bash
mkdir -p src/externals
vcs import --skip-existing src/externals < external.repos
rosdep install --from-paths src --ignore-src -r -y
colcon build --packages-up-to lk_diagnostics
source install/setup.bash
```

Launch the simulated publisher and aggregator:

```bash
ros2 launch lk_diagnostics lk_diagnostics_demo.launch.py
```

Inspect the raw and grouped diagnostics:

```bash
ros2 topic echo /diagnostics --once
ros2 topic echo /diagnostics_agg --once
ros2 topic echo /diagnostics_toplevel_state --once
```

## What It Shows

The simulated node publishes two diagnostic statuses:

- `connection_health`: `connected`, `sfu_ping_ms`, `num_peers`,
  `reconnect_count`, and `room_name`.
- `ros_graph`: `num_subbed_topics`, `num_pub_topics`, `num_services`, and
  `num_unmatched_topics`.

The aggregator groups `connection_health` under `LiveKit Bridge` and discards
`ros_graph` with a `DiscardAnalyzer`. This demonstrates how operators can
publish multiple LiveKit-related diagnostics while choosing which ones affect
the aggregate health view and `/diagnostics_toplevel_state`.

The status levels rotate through deterministic OK, WARN, and ERROR states.
Because `ros_graph` is discarded by the aggregator, only `connection_health`
affects the aggregate top-level status in this demo.

## Tradeoffs

Benefits:

- Uses standard `/diagnostics` and `/diagnostics_agg` topics.
- Works with existing ROS CLI, RQt, Foxglove, and application subscribers.
- Keeps bridge-facing status as structured key/value data.
- Demonstrates that users can filter low-priority diagnostics out of the
  aggregate health view.

Downsides:

- Adds the ROS diagnostics stack as a source dependency.
- Aggregation is configured with string/name matching.
- Periodic diagnostics are useful only if future real metrics stay curated and
  avoid noisy low-value fields.

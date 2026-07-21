# ros_portal

ROS 2 package containing the ROS Portal node, launch files, default
configuration, and ROS Portal tests.

ROS Portal dynamically discovers configured ROS 2 topics and services, forwards
matching entries to LiveKit, and republishes allowed
[LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/) and
[RPCs](https://docs.livekit.io/transport/data/rpc/) back into ROS 2. It also
exposes selected remote `ros2` CLI operations through ROS 2 services backed by
[LiveKit RPC](https://docs.livekit.io/transport/data/rpc/).

## Package Contents

- `src/`: ROS Portal node and library implementation.
- `include/`: public ROS Portal headers.
- `config/all_topics.yaml`: mirrors the builtin default config (used when no `config_path` is provided) that forwards all topics bidirectionally.
- `config/ros_portal.yaml`: example ROS Portal config used for development and integration testing.
- `launch/`: ROS Portal launch files for normal and local development use.
- `test/`: unit and integration tests.

ROS Portal-specific service interfaces live in the sibling
`ros_portal_msgs` package.

See the repository [README](../../README.md) for user and developer guides, and
the [data-track schema design](../../docs/design/schema.md) for the schema wire
contract and validation flow.

## Measuring latency

ROS Portal can measure the latency it adds. Enable it with `measure_latency: true`
in the ROS Portal config (see
[configuration.md](../../docs/configuration.md#measuring-latency-measure_latency-true)
for details). The whole T0..T5 timeline rides inside one typed message,
`ros_portal_msgs/LatencyTimestamps`:

- **T0** publisher stamps `t0` and publishes on `/ros_portal/latency/timestamp`.
- **T1/T2** the sending portal stamps callback entry and the LiveKit push.
- **T3/T4** the receiving portal stamps the LiveKit read and the ROS republish,
  emitting the message on `/ros_portal/latency/timestamp_rx`.
- **T5** the subscriber stamps arrival.

From those, `t2→t3` is the LiveKit transport and `(t2−t1) + (t4−t3)` is ROS
Portal's own added latency (`bridge_internal`). Cross-portal stamps are only valid
when the two portals share a wall clock, e.g. two portals on one host.

The two portals run in **separate ROS domains** so the probe traffic is forced
across LiveKit rather than shortcutting via local ROS discovery. The
`latency_probe_subscriber` node runs on the receiving domain, stamps T5, and
publishes rolling percentiles (p50/p90/p95/p99/min/max/mean per segment, in
milliseconds) as a `ros_portal_msgs/LatencyStats` on
`/ros_portal/latency/stats`.
Because the percentiles are computed on the graph, you watch latency **live** — no
recording or offline analysis is needed.

The `test_utilities` package ships the probe nodes used below.

### Characterize (two processes, real DDS)

Run two portals on one host in separate ROS domains, each with
`measure_latency: true` in its config. No `topics` entry is needed for the probe —
ROS Portal forwards the reserved latency topic automatically.

```bash
# Terminal 1 — sending portal (domain 1)
ROS_DOMAIN_ID=1 ros2 launch ros_portal ros_portal_local.launch.py \
  config:=<sender-config-with-measure_latency>.yaml identity:=robot
# Terminal 2 — receiving portal (domain 2)
ROS_DOMAIN_ID=2 ros2 launch ros_portal ros_portal_local.launch.py \
  config:=<receiver-config-with-measure_latency>.yaml identity:=controller

# Terminal 3 — probe publisher on the sending graph
ROS_DOMAIN_ID=1 ros2 run test_utilities latency_probe_publisher.py \
  --ros-args -p rate_hz:=100.0 -p payload_size:=1024

# Terminal 4 — subscriber on the receiving graph (publishes live stats)
ROS_DOMAIN_ID=2 ros2 run test_utilities latency_probe_subscriber.py
```

Watch latency live:

```bash
# Rolling percentiles per segment (e2e, bridge_internal, t2_t3, ...)
ROS_DOMAIN_ID=2 ros2 topic echo /ros_portal/latency/stats
# Or plot a field over time, e.g. PlotJuggler / rqt_plot on metrics[*].p95_ms
```

`bridge_internal` far below `t2_t3` (transport) is the headline — ROS Portal adds
negligible latency on top of LiveKit. Recording is optional: if you want to keep a
run, `ros2 bag record /ros_portal/latency/stats` captures the precomputed
percentiles directly (no offline analysis step).

### Regress (in-process, CI)

`test/integration/latency_e2e_test.cpp`
(`RosPortalTestE2E.BridgeInternalLatencyWithinBudget`) stands up two portals with
`measure_latency` enabled and asserts `bridge_internal` stays within a generous
budget, so a regression that adds real work to the forwarding path is caught. It
runs with the rest of the integration suite:

```bash
source ../../.token_helpers/set_test_tokens.bash
colcon test --packages-select ros_portal \
  --ctest-args -R ros_portal_integration_tests
colcon test-result --verbose
```

### See [../../README.md](../../README.md) for more information.

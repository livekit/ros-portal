# Latency tools

Trace capture, offline analysis, and visualization for the normal
ROS-to-LiveKit-to-ROS forwarding path.

```bash
ros2 launch latency_tools latency_measurement.launch.py

ros2 run latency_tools analyze_latency_trace.py <trace-directory> \
  --json latency.json --csv latency.csv

ros2 run latency_tools view_latency.py latency.json
```

The analyzer prints latency percentiles and optionally writes JSON aggregate
metrics and CSV per-frame samples. The viewer reads the JSON report and writes
`latency-results.png` in the current directory.

See the [latency guide](../../../docs/latency.md) for the complete workflow,
trace events, and regression-budget options.

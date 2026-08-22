# Stress testing

The developer stress harness runs one ROS Portal participant, a ROS workload,
a process/diagnostic monitor, and a LiveKit-only observer. It is not a CI test:
it needs a reachable LiveKit server and produces an inspectable baseline for
target hardware and network.

## Run

Build `ros_portal` and start a local LiveKit server. In the devcontainer,
`test_stress` prepares the ROS overlay, test-token environment, Python observer
environment, and runs the harness:

```bash
cbps ros_portal
test_stress --duration-s 600
```

The first invocation creates `.venv-stress` with the ROS system packages and
installs the LiveKit observer dependency. On a root Debian devcontainer it also
installs `python3-venv` when needed.

The defaults are the local-development values `devkey`, `secret`, and
`ws://127.0.0.1:7880`. Override them with `LIVEKIT_URL`, `LIVEKIT_API_KEY`,
and `LIVEKIT_API_SECRET`, or use matching `run.py` arguments. Each run uses an
isolated room by default; pass `--room` only when intentionally sharing one.
Artifacts, raw measurements, logs, and `report.md` are written below
`artifacts/stress/<timestamp>/`.

After warmup, the supervisor prints a `t=<elapsed>s` line once per second with Portal RSS and
CPU, RTC RTT/loss/bitrate, telemetry frames per second and average latency, and
video frames per second. Use the final report for percentiles and totals.

Press `Ctrl+C` to stop a run early. The harness reports that it was interrupted,
stops its child processes, and writes a partial report when the collected
artifacts are available; an intentional interruption is not treated as a test
failure.

## Default embedded-robot profile

| Stream | Default |
|---|---|
| Camera | one 640×480 BGR image stream at 30 fps, forwarded as H.264 at 3.5 Mbps |
| Telemetry | 100 Hz timestamped sequence numbers, with 192 bytes of payload padding |
| Service | one `std_srvs/Trigger` round trip per second |
| Warmup | 5 seconds, excluded from measured publishing |
| Duration | 10 minutes, excluding warmup |

Change resolution, frame rate, telemetry rate, service rate, duration, and
artifact location with `run.py --help`. Use at least a 30-minute run before
judging memory growth, then longer soaks to investigate suspected leaks.

## What to evaluate

The report captures delivery gaps and duplicates, telemetry latency, decoded
video frame count (after warmup), service latency/failures, ROS Portal RSS and CPU, host load,
and ROS Portal's LiveKit RTT/loss/jitter/send-bitrate diagnostics. The observer
uses LiveKit only; it does not subscribe to ROS, so delivery results include the
actual transport and remote SDK boundary.

For a local baseline, require no unexpected telemetry gaps or service failures,
compare video FPS with sent FPS, and record p50/p95/p99 latency under normal and
constrained links. Sustained RSS growth across repeated long runs, CPU near a
full core for this single-camera profile, transport loss causing delivery gaps,
or p99 latency beyond the intended control-loop budget are practical customer
dealbreakers. Disk is intentionally excluded: the harness does not record video
or bags, so it is not a ROS Portal steady-state cost.

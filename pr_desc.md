## Pause and resume ROS Portal forwarding (BOT-511)

Adds two `std_srvs/srv/Trigger` services to the ROS Portal node so an operator can suspend and restore all room-facing forwarding without restarting the process:

- `/ros_portal/pause` (`~/pause`)
- `/ros_portal/resume` (`~/resume`)

### How it works

`ConnectionManager` gains `pauseForwarding()` / `resumeForwarding()` / `isForwardingPaused()`. A pause clears the shared operations-enabled flag reported by `isOperationsEnabled()` — the single gate already consulted by every egress path (data-track pushes, video capture, schema definition, inbound/outbound RPC, service forwarding) — and latches the request under `session_mutex_` so connect and reconnect transitions cannot silently re-enable it. Waiters blocked in `waitForOperations()` are released with `false` rather than parked for the duration of a pause.

While paused, the LiveKit room connection is held (so `resume` continues without a fresh join) and ROS subscriptions stay in place; `pollConnection()` skips starting room components so it doesn't retry against the closed gate every tick.

### Service contract

Both services are idempotent, and `success` is `false` only when the node has not initialized (e.g. missing LiveKit credentials):

| Call | State | `success` | `message` |
|---|---|---|---|
| resume | running | `true` | `ROS Portal is already running` (+ `; waiting for a room connection`) |
| resume | paused | `true` | `ROS Portal resumed` / `…; waiting for a room connection` |
| pause | paused | `true` | `ROS Portal is already paused` |
| pause | running | `true` | `ROS Portal paused` |
| either | uninitialized | `false` | `ROS Portal is not initialized` |

The services are advertised from the node constructor, so they answer for the node's whole lifetime instead of timing out when initialization failed.

### Diagnostics

`ros_portal_status` publishes `forwarding_paused` and summarizes `WARN "ROS Portal operations are paused"` — reported ahead of the component-inactivity `ERROR`, since a pause explains it — so a paused portal is never reported as healthy.

### Tests

- `connection_manager_test.cpp` — idempotent pause/resume; pause surviving a fresh connect *and* an in-session reconnect; waiters released rather than blocked; resume-before-connect deferring until connected.
- `ros_portal_pause_resume_test.cpp` (new) — services answered over real ROS clients on an executor, uninitialized rejection, node-level idempotency, deferred resume, diagnostics reporting.
- `ros_portal_e2e_test.cpp` — `PauseSuspendsForwardingAndResumeRestoresIt`: verifies A→B forwarding, pauses and proves nothing crosses during a negative-assertion window, then resumes and verifies forwarding again.

Verified in the devcontainer (Jazzy): unit + integration `629 tests, 0 errors, 0 failures`; launch tests green; `clang-format` clean. Adds `std_srvs` as a package dependency (was test-only) and documents the services in `docs/running.md` / `docs/diagnostics.md`.

### Known gap (pre-existing, not introduced here)

While operations are disabled, the inbound RPC wrapper returns `std::nullopt`, which the SDK sends as a response with no error and no payload — so a peer calling this portal's `ros2_*` methods sees `remote <method> returned malformed JSON` instead of a real RPC error. The gate behaved this way during reconnect windows already; `~/pause` just makes it easy to hit. Fix would be to throw `livekit::RpcError` from the wrapper; happy to fold in here or file a follow-up.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

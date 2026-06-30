# ServiceForwarder: ROS 2 services ↔ LiveKit RPC

`ServiceForwarder`
([include/ros2_livekit_bridge/service_forwarder.hpp](../include/ros2_livekit_bridge/service_forwarder.hpp),
[src/service_forwarder.cpp](../src/service_forwarder.cpp))
bridges the ROS 2 **request/response** world to LiveKit **RPC**, in both
directions. It is the service analogue of `TopicForwarder` (which bridges
ROS topics ↔ LiveKit data/video tracks).

It owns **no** `livekit::Room`. The bridge (`Ros2LiveKitBridge`) keeps the room
at the edge and hands the forwarder a small set of RPC callbacks
(`LiveKitMethods`) plus a non-owning `rclcpp::Node::WeakPtr`. All ROS endpoints
are created on that node; all LiveKit traffic goes through the supplied
callbacks.

---

## Configuration

Services are declared in the bridge config under `services:`
([config/ros2_livekit_bridge.yaml](../config/ros2_livekit_bridge.yaml),
schema in `ros2_livekit_bridge_config`). Each entry:

```yaml
services:
  - service: "/remote/add_two_ints"          # ROS service name
    msg_type: "example_interfaces/srv/AddTwoInts"  # ROS service type
    direction: "out"                          # in | out | bidirectional
    participant: "robot-a"                    # remote LiveKit identity
```

`utils::serviceForwarderEntries()`
([src/utils/ros_utils.cpp](../src/utils/ros_utils.cpp)) maps the parsed config
into `ServiceForwarder::ServiceForwarderEntry` values, and
`Ros2LiveKitBridge::initializeServiceForwarder()`
([src/ros2_livekit_bridge.cpp](../src/ros2_livekit_bridge.cpp)) constructs the
forwarder after the room connects. All endpoints are created **eagerly in the
constructor** — `msg_type` is known up front, so unlike topic forwarding there
is no graph polling step.

| Field | Meaning |
|---|---|
| `service` | ROS service name. Identical on both peers; the RPC method name is derived from it. |
| `msg_type` | ROS service type. Used to load runtime type support for (de)serialization. |
| `direction` | `out` = proxy a remote service locally; `in` = expose a local service over RPC; `bidirectional` = both. |
| `participant` | For `out`: the remote LiveKit identity to call. For `in`: the expected peer (informational today). |

---

## Endpoints at a glance

| Direction | ROS endpoint created | LiveKit endpoint created | Triggered by |
|---|---|---|---|
| `out` | A **proxy service server** (`GenericService`) advertised at `service` | Calls `performRpc(participant, method, …)` | A local ROS client calling `service` |
| `in`  | A **service client** (`GenericServiceClient`, created lazily) to the real local `service` | Registers an RPC handler under `method` (`registerRpcMethod`) | A remote participant invoking `method` |
| `bidirectional` | both of the above | both of the above | — |

`method` is `ServiceForwarder::rpcMethodName(service)` (see
[RPC method naming](#rpc-method-naming)). Because it is derived purely from the
service name, the `out` caller and the `in` handler on the peer compute the
**same** method name with no negotiation.

> ⚠️ **Bidirectional self-loop:** on a single node the `out` proxy server and
> the `in` handler's target share the `service` name, so the proxy would call
> itself. `bidirectional` is meant for symmetric two-robot setups where each
> side owns a real local server *and* a proxy to the peer under **distinct**
> names. The constructor logs a warning for each `bidirectional` entry.

---

## Data formats and where conversion happens

The payload crosses four representations. The transitions are the core of this
component:

```
ROS typed message  <--(de)serialize-->  CDR bytes  <--base64-->  ASCII string  <--JSON envelope (responses only)-->  LiveKit RPC payload (UTF-8 string)
 (rcl C struct,         rclcpp            std::vector             std::string                 nlohmann::json                 over the SFU
  DynamicMessage)    Serialization      <uint8_t>                                            {"ok","resp_b64","err"}
```

- **ROS typed message** — the native, runtime-typed message. Held as a
  `DynamicMessage` ([include/ros2_livekit_bridge/ros2_cli/dynamic_message.hpp](../include/ros2_livekit_bridge/ros2_cli/dynamic_message.hpp)),
  an introspection-backed buffer, because the type is only known at runtime.
- **CDR bytes** — the wire serialization ROS uses (`rclcpp::SerializedMessage`,
  exposed as `std::vector<std::uint8_t>`). This is what travels over RPC,
  base64-wrapped. Conversion uses the runtime serializer in `ServiceTypeSupport`
  ([include/ros2_livekit_bridge/service_type_support.hpp](../include/ros2_livekit_bridge/service_type_support.hpp)).
- **base64** — LiveKit RPC payloads are UTF-8 strings, so binary CDR is
  base64-encoded. `utils::base64Encode` / `base64Decode`
  ([include/ros2_livekit_bridge/utils/base64.hpp](../include/ros2_livekit_bridge/utils/base64.hpp)).
  Inflation is ~4/3, so the ~15 KiB payload cap leaves ~11 KiB of raw CDR.
- **JSON envelope** — only the **response** is wrapped in JSON, so the out-side
  can tell a real result from a remote error. `service_rpc_codec`
  ([include/ros2_livekit_bridge/service_rpc_codec.hpp](../include/ros2_livekit_bridge/service_rpc_codec.hpp)).

### What is on the wire

| Hop | Payload format |
|---|---|
| ROS client ↔ proxy server (`out`) / real server ↔ client (`in`) | Native ROS typed message (CDR on the DDS wire, handled by rmw) |
| **RPC request** payload (`performRpc` arg / `RpcInvocationData.payload`) | **base64(request CDR)** — a bare ASCII string, no JSON wrapper |
| **RPC response** payload (return of `performRpc` / RPC handler) | **JSON**: `{"ok":true,"resp_b64":"<base64(response CDR)>"}` on success, `{"ok":false,"err":"<reason>"}` on failure |

The request is a bare base64 string because it is always data; the response
needs the `ok`/`err` envelope because a ROS service has no in-band error channel
(see [Failure handling](#failure-handling)).

---

## Outbound flow (`out`): local ROS client → remote ROS server

The bridge node hosts a **proxy** server; the real service lives on
`participant`.

```
local ROS client
   │  ROS service call to "/foo"  (typed request, CDR on DDS wire)
   ▼
GenericService (proxy server on the bridge node)            [ROS executor thread]
   │  executor: take_type_erased_request → DynamicMessage (typed request)
   │  handle_request: serialize_message  ──►  request CDR (vector<uint8_t>)
   ▼
ServiceForwarder::forwardOutboundCall
   │  has_participant(participant)?  no → drop (client times out)
   │  encodeServiceRequest(cdr)      ──►  base64 string
   │  size > 15 KiB?                 yes → drop
   │  perform_rpc(participant, "ros2_srv:foo", base64, rpcTimeout)
   ▼
LiveKit  LocalParticipant::performRpc(destination_identity, method, payload, timeout)
   │      … SFU → remote participant's registered handler → response …
   │  returns the JSON envelope string (or nullopt on RpcError/timeout)
   ▼
ServiceForwarder::forwardOutboundCall
   │  decodeServiceResponse(envelope) → { ok, response CDR } or { error }
   │  ok? no → drop (client times out)
   ▼
GenericService::handle_request
   │  deserialize_message(response CDR) → DynamicMessage (typed response)
   │  rcl_send_response
   ▼
local ROS client  (typed response)
```

Outbound never needs a JSON request wrapper and never touches type support for
the request body beyond serialization — `GenericService` already produced the
CDR.

## Inbound flow (`in`): remote ROS client → local ROS server

This is the second half, running on the bridge whose node actually hosts `/foo`.

```
remote participant  (the out-side proxy of a peer bridge)
   │  performRpc → RPC method "ros2_srv:foo", payload = base64(request CDR)
   ▼
Ros2LiveKitBridge::rpcRegisterMethod adapter            [LiveKit room event thread]
   │  SDK delivers RpcInvocationData{ caller_identity, payload (UTF-8 ≤15 KiB), … }
   │  passes data.payload (string) to the RpcHandler
   ▼
ServiceForwarder::handleInboundRpc
   │  decodeServiceRequest(payload) → request CDR (vector<uint8_t>)  (base64 decode)
   │  invalid base64 → encodeServiceResponse(false, …, "invalid base64 …")
   │  lazily create GenericServiceClient("/foo", msg_type)
   │  serviceIsReady()? no → encodeServiceResponse(false, …, "… not available")
   ▼
GenericServiceClient::call(request CDR, timeout)
   │  deserialize_message(request CDR) → DynamicMessage (typed request)
   │  rcl_send_request  →  local ROS service "/foo"
   │  poll take_type_erased_response (sequence-matched, stale-drained)
   │  serialize_message(response)  ──►  response CDR
   ▼
ServiceForwarder::handleInboundRpc
   │  encodeServiceResponse(true, response CDR, "")  →  JSON envelope string
   │  size > 15 KiB? → encodeServiceResponse(false, …, "… size limit")
   ▼
returns the JSON envelope string → SDK sends it as the RPC response
```

---

## RPC method naming

`ServiceForwarder::rpcMethodName(service)`:

- Strip a leading `/`, then sanitize with `utils::sanitizeRosNameToken`
  (keep `[A-Za-z0-9_]`, replace anything else with `_`).
- Prefix with `ros2_srv:`.
- Examples: `/set_bool` → `ros2_srv:set_bool`; `/turtle1/set_pen` →
  `ros2_srv:turtle1_set_pen`.
- LiveKit caps method names at **64 bytes**. If the readable name would exceed
  that, the token is truncated and an 8-hex FNV-1a hash of the **full original
  service name** is appended, keeping the result deterministic and identical on
  both ends.

---

## Components

| File | Namespace / symbol | Responsibility |
|---|---|---|
| [service_forwarder.hpp](../include/ros2_livekit_bridge/service_forwarder.hpp) / [.cpp](../src/service_forwarder.cpp) | `ros2_livekit_bridge::ServiceForwarder` | Orchestrates both directions; owns the per-service state maps and the reentrant callback group. |
| [generic_service.hpp](../include/ros2_livekit_bridge/generic_service.hpp) / [.cpp](../src/generic_service.cpp) | `ros2_livekit_bridge::GenericService` | Type-erased ROS service **server** (Jazzy has no generic server). Subclasses `rclcpp::ServiceBase` + `rcl_service_init`. Callback contract: `std::optional<std::vector<uint8_t>>(std::vector<uint8_t> request_cdr)` — CDR in, CDR out, `nullopt` to drop. |
| [generic_service_client.hpp](../include/ros2_livekit_bridge/generic_service_client.hpp) / [.cpp](../src/generic_service_client.cpp) | `ros2_livekit_bridge::GenericServiceClient` | Type-erased ROS service **client**, CDR in / CDR out. Poll-based (`take_type_erased_response`), so it is safe to call from a non-executor thread. |
| [service_type_support.hpp](../include/ros2_livekit_bridge/service_type_support.hpp) / [.cpp](../src/service_type_support.cpp) | `ros2_livekit_bridge::ServiceTypeSupport`, `MessageTypeSupport` | Loads runtime type support (serialization + introspection) for a `pkg/srv/Type`. Shared with `Ros2ServiceCall`. |
| [service_rpc_codec.hpp](../include/ros2_livekit_bridge/service_rpc_codec.hpp) / [.cpp](../src/service_rpc_codec.cpp) | `encodeServiceRequest` / `decodeServiceRequest` / `encodeServiceResponse` / `decodeServiceResponse` | base64 + JSON-envelope framing for the RPC payloads. Defines `kMaxRpcPayloadBytes` (15 KiB). |
| [utils/base64.hpp](../include/ros2_livekit_bridge/utils/base64.hpp) / [.cpp](../src/utils/base64.cpp) | `ros2_livekit_bridge::utils::base64Encode` / `base64Decode` | Standard base64; decode validates length, alphabet, and padding. |

The LiveKit boundary is the bridge's `LiveKitMethods` callbacks, wired in
[src/ros2_livekit_bridge.cpp](../src/ros2_livekit_bridge.cpp):

- `perform_rpc` → `livekit::LocalParticipant::performRpc(destination_identity, method, payload, timeout_sec)`.
- `register_rpc_method` / `unregister_rpc_method` → `LocalParticipant::registerRpcMethod` / `unregisterRpcMethod`. The bridge adapts the SDK handler (`RpcInvocationData` → `std::optional<std::string>`) to the forwarder's `RpcHandler` (`std::string(const std::string&)`), passing `RpcInvocationData.payload` straight through.
- `has_participant` → `Room::remoteParticipant(identity)`.

---

## Threading

- **Outbound** — `GenericService::handle_request` (and thus
  `forwardOutboundCall` → `perform_rpc`) runs on a **ROS executor worker
  thread** (the forwarder's reentrant callback group; the bridge runs a
  `MultiThreadedExecutor` with `ros_threads`). `perform_rpc` blocks that worker
  for the RPC round-trip. The reentrant group lets other requests proceed on
  other workers.
- **Inbound** — the RPC handler runs on the **LiveKit room event thread**, not a
  ROS executor thread. `GenericServiceClient::call` polls
  `take_type_erased_response` directly (it does not rely on executor dispatch),
  so it works from that thread; it blocks the event thread for the duration of
  the local service call. This matches the existing `Ros2CliManager` precedent.

---

## Timeouts and size limits

- `ServiceForwarderOptions::service_call_timeout_sec` defaults to
  `kDefaultTimeoutSec` (10 s).
- **Inbound:** `GenericServiceClient::call` waits up to that timeout for the
  local ROS service to respond.
- **Outbound:** the LiveKit RPC timeout is
  `ServiceForwarder::rpcTimeout(service_call_timeout_sec)` =
  `service_call_timeout_sec + kServiceCallRpcTimeoutMarginSec` (1 s, saturating
  at 255), so the RPC round-trip outlives the remote ROS service-call wait.
- **Payload cap:** `kMaxRpcPayloadBytes` = 15 KiB applies to the base64/JSON
  string actually sent over RPC, checked on the outbound request and the inbound
  response before transport. Raw CDR is effectively capped at ~11 KiB.

---

## Failure handling

A ROS service exposes no in-band error channel — a call either returns a typed
response or times out. So failures collapse to "no response":

- **Outbound** (`forwardOutboundCall` returns `std::nullopt` → `GenericService`
  sends nothing → local client times out) when: the participant is absent, the
  request exceeds the size cap, `perform_rpc` fails (RPC error/timeout/recipient
  gone), or the response envelope reports `ok:false` / fails to decode.
- **Inbound** (`handleInboundRpc` returns an `{"ok":false,"err":…}` envelope)
  when: the request payload is not valid base64, the ROS node/type support is
  unavailable, the local service is not ready, the call fails or times out, or
  the response would exceed the size cap. The peer's outbound side then surfaces
  this as a dropped response (its caller times out).

Transient outbound failures are logged with a 5 s throttle; inbound errors are
returned in the envelope.

---

## Lifecycle

- Built by `Ros2LiveKitBridge::initializeServiceForwarder()` after the room
  connects; all servers/handlers are set up in the constructor.
- The destructor first **unregisters** every inbound RPC method (so the SDK
  stops invoking handlers while the local participant is still alive), then
  destroys the outbound proxy servers (`rcl_service_fini` runs in the rcl handle
  deleter). The bridge resets `service_forwarder_` **before** the room, and
  after the executor has stopped spinning.

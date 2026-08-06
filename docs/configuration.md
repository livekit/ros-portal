# Configuration

ROS Portal is configured via environment variables for LiveKit parameters and
a YAML configuration file for runtime parameters.

## Prerequisites

Follow [Installation](./installation.md) before this guide.

This guide assumes the following:

- LiveKit CLI is installed and can be run via `lk`
- If using LiveKit Cloud, a project is setup with an API key and secret available

## Environment Variables

Environment variables are used to configure ROS Portal's connection and access to a
LiveKit server.

| Variable | Required | Description |
|---|---:|---|
| `LIVEKIT_URL` | yes | WebSocket URL of the LiveKit server. |
| `LIVEKIT_TOKEN` | yes | LiveKit access JWT for the ROS Portal participant. |
| `LIVEKIT_API_KEY` | no | LiveKit Cloud only: API key used to mint `LIVEKIT_TOKEN` with LiveKit CLI (not read by ROS Portal). |
| `LIVEKIT_API_SECRET` | no | LiveKit Cloud only: API secret used to mint `LIVEKIT_TOKEN` with LiveKit CLI (not read by ROS Portal). |

### Token Configuration

`LIVEKIT_TOKEN` allows ROS Portal to connect to the LiveKit room and is minted
using `lk token create`. The following table lists the required token options:

| Option/Grant | `lk` flag | Why ROS Portal needs it |
|---|---|---|
| Room join | `--join` | Connect to the LiveKit room. |
| Room name | `--room <name>` | Identifies the room to join. |
| Identity | `--identity <identity>` | Identifies the ROS Portal participant. |
| `canUpdateOwnMetadata` | `--allow-update-metadata` | ROS Portal advertises its LiveKit participant with the `lk.robot` attribute set to `"true"`. |

### Development Server Examples

Use the following two example commands to quickly configure the environment for a local
LiveKit server instance (started using `--dev`):

```bash
export LIVEKIT_URL=ws://127.0.0.1:7880
```

```bash
export LIVEKIT_TOKEN="$(lk token create \
  --join \
  --room robo_room \
  --identity ros-portal \
  --allow-update-metadata \
  --valid-for 24h \
  --token-only \
  --yes \
  --dev)"
```

### LiveKit Cloud Examples

Use the following commands as reference for configuring the environment for a LiveKit Cloud
project:

```bash
export LIVEKIT_URL=wss://<project details>.livekit.cloud
```

Export `LIVEKIT_API_KEY` / `LIVEKIT_API_SECRET` from the project before minting `LIVEKIT_TOKEN`:

```bash
export LIVEKIT_API_KEY=<api-key>
export LIVEKIT_API_SECRET=<api-secret>
export LIVEKIT_TOKEN="$(lk token create \
  --join \
  --room robo_room \
  --identity ros-portal \
  --allow-update-metadata \
  --valid-for 24h \
  --token-only \
  --yes)"
```

## Configuration File

The YAML configuration file defines which ROS topics and services ROS Portal
forwards through LiveKit, in which direction, and related runtime options.
The configuration file path is resolved through the node's `config_path` ROS
parameter. See [Running](./running.md) for more information.

Follow the examples below to get started quickly, or use the field reference
further down to build out a full configuration file.

### Examples

Complete configs in this repository:

- [`ros_portal.yaml`](../src/ros_portal/config/ros_portal.yaml) — default package config used for development and integration testing.
- [`turtle_sim_config.yaml`](../src/ros_portal_tutorials/config/turtle_sim_config.yaml)
  and
  [`turtle_sim_controller.yaml`](../src/ros_portal_tutorials/config/turtle_sim_controller.yaml) —
  paired turtlesim tutorial configs (see [Tutorials](./tutorials.md)).

Full robot stacks with ROS Portal configs:

- `cobra_flex_ros` [`livekit.yaml`](https://github.com/livekit-examples/cobra_flex_ros/blob/main/bringup/config/livekit.yaml) —
  Waveshare Cobra Flex teleoperation.
- `waver_ros` [`livekit_robot.yaml`](https://github.com/livekit-examples/waver_ros/blob/main/src/waver_bringup/config/livekit_robot.yaml)
  and
  [`livekit_controller.yaml`](https://github.com/livekit-examples/waver_ros/blob/main/src/waver_bringup/config/livekit_controller.yaml) —
  WAVE ROVER robot and controller sides.

### Minimal Config

```yaml
ros_portal:
  version: "0.0.1"
```

### Top-Level Fields

All config lives under `ros_portal`.

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `version` | string | yes | - | Configuration schema version, currently `"0.0.1"`. |
| `ros_threads` | integer | no | `0` | ROS executor thread count. `0` uses the available CPU-core count, matching `rclcpp` default. |
| `services` | list | no | `[]` | Service route declarations. |
| `topics` | list | no | `[]` | Topic route declarations. |

### Services

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `service` | string | yes | - | ROS service name. Must be non-empty. |
| `direction` | string | yes | - | `out` — the only supported value. |
| `participant` | string | yes | - | LiveKit participant identity. Must be non-empty. |
| `msg_type` | string | yes | - | ROS service type, such as `std_srvs/srv/SetBool`. Must be non-empty. |

`direction: "out"` creates a local ROS service server that forwards calls to
the configured LiveKit participant using `msg_type`.

Services only accept `out`. `in` and `bidirectional` are **not** used for
services: ROS Portal does not attempt to mirror the ROS 2 service graph in both
directions. A service is a point-to-point request/response call, so a route is
fully described by the single participant that answers it. This is unlike
topics (see below), where `in` / `out` / `bidirectional` are meaningful and are
used to limit which streams cross ROS Portal for bandwidth reasons.

### Topics

| Field | Type | Required | Default | Description |
|---|---:|---:|---:|---|
| `topic` | string | yes | - | ROS topic pattern. Must be non-empty. Treated as an ECMAScript regex for the [DataTrack](https://docs.livekit.io/transport/data/data-tracks/) path; matched as a literal name for `max_rate_hz`, `latched`, and `encoding`. |
| `direction` | string | yes | - | `in`, `out`, or `bidirectional`. |
| `preserve_id` | boolean | no | `false` | Inbound topics only. Prefix the republished ROS topic with the publishing participant's identity. |
| `max_rate_hz` | number | no | - | Outbound topics only. Cap (in Hz) on the rate samples are forwarded to LiveKit; samples arriving within one period of the last forwarded one are dropped (like `topic_tools throttle messages`). Literal topic names only. |
| `latched` | boolean | no | `false` | Treat the topic as latched (see below). Literal topic names only. |
| `encoding` | string | no | `ros2msg` | Outbound topics only. `ros2msg`, `ros2idl`, or `jsonschema` — selects how data is encoded on the DataTrack (see below). Literal topic names only. |
| `video_options` | map | no | - | Optional video publish settings. |

Outgoing topics are those with `direction: "out"` or
`direction: "bidirectional"`. Incoming topics are those with `direction: "in"`
or `direction: "bidirectional"`. Topic patterns are ECMAScript regular
expressions matched against the full topic name.

Unlike services, topic direction is load-bearing: it controls which topics are
forwarded and in which direction. Only forwarding the streams you actually need
(and only in the required direction) keeps unnecessary traffic off the LiveKit
connection, which matters for bandwidth on constrained links.

### Topic discovery

Outbound `topic` is an ECMAScript regex. ROS Portal waits for ROS graph-change
events, takes one shared topic snapshot, and creates subscriptions for new
matches. Subscriptions whose publishers remain absent for 30 seconds are
removed; they are recreated if a matching publisher later reappears.

Discovery has no tuning knob. Graph changes wake it immediately, and the only
timed wake-up it needs — revisiting a subscription whose 30-second grace is
about to elapse — is derived from that grace rather than configured.

### Preserving the publisher identity

`preserve_id` applies only to inbound (`in` / `bidirectional`) topics and is
ignored for outbound topics. It defaults to `false` to preserve the original
topic name, so an inbound data track is republished under its own name (for
example, `/imu` stays `/imu`). When set
to `true`, the publishing participant's identity is prepended to the
republished ROS topic name, which prevents collisions when multiple
participants publish the same topic:

```yaml
topics:
  - topic: "/imu"
    direction: "in"
    preserve_id: true
```

```text
LiveKit data track: /imu   (from participant "robot-1")
ROS topic:          /robot_1/imu
```

The identity is sanitized into a legal ROS name token, so any character that is
not alphanumeric or `_` becomes `_` (e.g. `robot-1` → `robot_1`).

#### Capping the outbound forward rate

`max_rate_hz` applies only to outbound (`out` / `bidirectional`) topics and is
ignored for inbound topics. The behavior mirrors
[`topic_tools throttle messages`](https://github.com/ros-tooling/topic_tools#throttle):
samples are forwarded **on arrival**, but a sample is dropped if it arrives less
than one period (`1 / max_rate_hz`) after the last one that was forwarded. This
keeps a high-rate topic from saturating a constrained LiveKit link when a remote
consumer only needs updates at a lower rate — for example, throttling a robot's
`/imu` (often 30–100+ Hz) down to a viewer-friendly rate:

```yaml
topics:
  - topic: "/imu"
    direction: "out"
    max_rate_hz: 10
```

The first sample within each period is the one forwarded; later samples in the
same period are dropped. Only newly arriving samples are ever forwarded, so an
idle topic is never re-sent and the cap never acts as a rate *floor*. On a
backward clock jump (e.g. a sim-time reset) the throttle window is reset so
forwarding does not stall until the old timestamp is reached again.

Trade-offs to be aware of:

- **Effective rate tracks the input.** Because samples are gated on arrival,
  the achieved rate is at most `max_rate_hz` and is bounded by the source rate;
  a slow or bursty source produces fewer forwards than the cap.
- **Lossy for aggregate topics.** Only the first sample per period survives, so
  this is *not* a faithful downsample of topics whose messages each carry
  distinct content. `/tf` is the notable case: it aggregates transforms from
  multiple broadcasters in separate messages, so capping it can drop some
  broadcasters' transforms. It works well for large, whole-state messages where
  only the latest value matters (maps, costmaps, images-as-data, robot
  descriptions, imu, etc).

Unlike `topic` (an ECMAScript regex), `max_rate_hz` is matched by **literal
topic name** — the cap applies to a discovered topic only when its name equals
the configured `topic` string exactly.

#### Latched topics (`latched: true`)

Some ROS topics are **latched**: they are published once (or rarely) with
`RELIABLE` + `TRANSIENT_LOCAL` durability, and DDS replays the last sample to
subscribers that join later. `/tf_static` is the canonical example — static
transforms are broadcast once at startup, and a node that starts afterward still
receives them.

[LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/) are
**not** latched: a frame pushed before a peer subscribes is lost, and a static
topic never republishes it. Setting `latched: true` routes the topic off the
data-track path and onto a reliable
[**RPC**](https://docs.livekit.io/transport/data/rpc/) push-with-ack mechanism
instead:

- **Outbound** (`out` / `bidirectional`): ROS Portal subscribes with
  `TRANSIENT_LOCAL` QoS (so it captures state published before ROS Portal
  started), stores the topic's distinct messages, and a background worker pushes
  them to every peer ROS Portal node in the room until each acknowledges them
  over RPC. State is re-pushed only when it changes or a peer rejoins, so
  steady-state traffic is zero. A peer that repeatedly fails (for example, a
  participant without a ROS Portal handler) is dropped after a few attempts and
  retried only when new state arrives.
- **Inbound** (`in` / `bidirectional`): ROS Portal republishes received messages
  on a `TRANSIENT_LOCAL` publisher, so ROS subscribers that start after ROS
  Portal (RViz, Foxglove, and tf listeners) still receive the latched state.
  The ROS message type travels in the RPC payload, so ROS Portal creates the
  republishing publisher without any additional type configuration.

Like `max_rate_hz`, `latched` is matched by **literal topic name**, not regex.

**Payload limit.** The push uses a
[LiveKit RPC](https://docs.livekit.io/transport/data/rpc/) whose payload is capped at
**15 KiB** (base64 leaves ~11 KiB of message bytes per call). A single message
larger than that cannot be sent and is skipped with a logged warning. This is
ample for typical `/tf_static` trees (a few hundred short-named transforms), but
large monolithic latched topics (e.g. `/robot_description`, large `/map`) are out
of scope for this path — keep them on the
[DataTrack path](https://docs.livekit.io/transport/data/data-tracks/) or handle them out of
band.

```yaml
topics:
  # dynamic transforms stay on the DataTrack path
  - topic: "/tf"
    direction: "out"

  # static transforms are latched
  - topic: "/tf_static"
    direction: "out"
    latched: true
```

On the consuming ROS Portal node:

```yaml
topics:
  - topic: "/tf_static"
    direction: "in"
    latched: true
```

#### Data track encoding (`encoding`)

`encoding` applies only to outbound (`out` / `bidirectional`) topics and selects
how this ROS Portal node encodes a topic's messages on the LiveKit DataTrack
and which schema it advertises to subscribers:

| Value | Frame format | Advertised schema | Use case |
|---|---|---|---|
| `ros2msg` (default) | ROS CDR | ROS 2 Message | ROS-to-ROS forwarding; no conversion cost. |
| `ros2idl` | ROS CDR | ROS 2 IDL | ROS-to-ROS forwarding where consumers expect an IDL schema. |
| `jsonschema` | JSON | generated JSON Schema | Communicating with **non-ROS** systems that read JSON directly. |

```yaml
topics:
  # Publish /diagnostics as JSON so a non-ROS dashboard can consume it directly.
  - topic: "/diagnostics"
    direction: "out"
    encoding: "jsonschema"
```

Notes:

- `jsonschema` deserializes each ROS message and sends a JSON frame described by
  a JSON Schema generated from the ROS type. A peer ROS Portal node receiving
  the track transparently converts the JSON back into the ROS message before
  republishing, so ROS-to-ROS forwarding still works with any encoding.
- `ros2idl` requires the local ROS type definition to be renderable as ROS 2
  IDL. A topic requesting an encoding whose schema cannot be rendered is skipped
  with an error.
- Inbound decoding is independent of this setting: it is auto-detected from the
  remote track's advertised frame encoding (CDR or JSON). `encoding` is ignored
  for pure inbound (`in`) topics.
- Like `max_rate_hz` and `latched`, `encoding` is matched by **literal topic
  name**, not regex.

### Video Options

| Field | Type | Required | Description |
|---|---:|---:|---|
| `bitrate_kbps` | integer | no | Target bitrate in kbps. Must be positive. |
| `codec` | string | no | Video codec name. Must be non-empty when set. |

Audio options are not part of config version `0.0.1`.

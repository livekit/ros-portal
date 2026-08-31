# Architecture

`ros_portal` is a ROS2 node that bridges selected ROS2 topics and
services to other participants in a LiveKit room. It uses
[LiveKit DataTracks](https://docs.livekit.io/transport/data/data-tracks/) for
ROS message streaming and [LiveKit RPC](https://docs.livekit.io/transport/data/rpc/)
for remote introspection and latched-topic delivery.

## ROS Portal Node

ROS Portal is implemented as a single ROS2 node (`RosPortal`) that:

1. Parses parameters from a YAML config declaring which topics and services to
   route.
2. Waits for ROS graph-change events, then takes one shared snapshot with
   `get_topic_names_and_types()`. This is a DDS graph-cache lookup and does not
   add traffic to the network or affect other nodes.
3. Matches discovered topics against the configured ECMAScript regular
   expressions.
4. Creates subscriptions for each newly matched topic, using a QoS profile
   aggregated from all active publishers.
5. Uses a typed subscription for `sensor_msgs/msg/Image` topics so frames can be
   pushed into a LiveKit video track.
6. Uses `rclcpp::GenericSubscription` for other topics and forwards raw
   CDR-serialized bytes over a
   [LiveKit DataTrack](https://docs.livekit.io/transport/data/data-tracks/) with
   a self-contained ROS message schema.
7. Subscribes to allowed remote LiveKit data tracks, validates their schemas
   against the local ROS installation, and republishes accepted CDR payloads
   into ROS using the track name as the local topic name.

## Topic Discovery

For outbound topic patterns, ROS Portal reconciles subscriptions after each ROS
graph-change event. It creates subscriptions for newly matched topics and
removes subscriptions whose publishers have remained absent for 30 seconds.
When a publisher for a matching topic reappears, ROS Portal recreates the
subscription.

Graph changes wake discovery immediately. Its only timed wake-up is to revisit
a subscription when its 30-second publisher-absence grace period expires; this
timeout is derived from the grace period and is not configurable.

## Message-Type Handling

| ROS2 message type | LiveKit track type | Wire format | Behavior |
| --- | --- | --- | --- |
| `sensor_msgs/msg/Image` | Video track | RGBA pixels | A `livekit::VideoSource` and `livekit::LocalVideoTrack` are created lazily on the first received frame. Each callback converts the image to RGBA and pushes the frame through `VideoSource::captureFrame()`. Supported encodings are `rgba8`, `rgb8`, `bgr8`, `bgra8`, and `mono8`. |
| Any other type | [DataTrack](https://docs.livekit.io/transport/data/data-tracks/) | ROS 2 CDR | A generic subscription is created using the type string discovered from the ROS graph. Incoming serialized message buffers are pushed verbatim onto a `livekit::LocalDataTrack`. |

The data-track payload is the unmodified CDR byte stream produced by the
publisher. ROS Portal registers the matching recursive ROS message definition
with LiveKit so consumers can discover the schema needed to decode it.

For LiveKit-to-ROS data tracks, the track name supplies the ROS topic name and
the schema ID supplies a candidate ROS message type. An existing local graph
type takes precedence. Before creating an `rclcpp::GenericPublisher`, ROS Portal
requires the remote schema encoding, SHA-256 hash, and exact definition text to
match the locally rendered definition. See
[Data Track Schema Design](schema.md) for the wire contract and validation
flow.

```text
ROS topic publisher
  -> RosPortal subscription
  -> LiveKit local video/data track
  -> LiveKit room
  -> remote ROS Portal node
  -> ROS publisher
```

## LiveKit-To-ROS Topic Names

Inbound data tracks are published on ROS using the LiveKit track name directly:

```text
LiveKit data track: /odom/global
ROS topic:          /odom/global
```

Track names without a leading `/` are normalized to absolute ROS topic paths.
When the matching inbound topic sets `preserve_id: true`, the publishing
participant's sanitized identity is prepended to the local topic name. See
[Configuration](../configuration.md#preserving-the-publisher-identity).

## Topic Direction

Topic direction controls which streams cross ROS Portal:

- `out`: allow ROS-to-LiveKit forwarding.
- `in`: allow LiveKit-to-ROS forwarding.
- `bidirectional`: allow both directions.

Only forwarding streams that are needed keeps unnecessary traffic off the
LiveKit connection, which matters on constrained links.

Services are different. They only accept `direction: "out"` because ROS Portal
does not mirror the ROS2 service graph in both directions. A service call is
point-to-point, so a route is fully described by the participant that answers
it.

## Remote ROS2 CLI Calls

ROS Portal exposes ROS2 services backed by
[LiveKit RPC](https://docs.livekit.io/transport/data/rpc/) that run a subset of
`ros2` CLI commands against other connected ROS Portal nodes, including:

- `ros2 topic list`
- `ros2 topic pub`
- `ros2 service list`
- `ros2 service call`
- `ros2 interface show`

See [Remote ROS2 CLI calls](../ros2_cli_calls.md) for request fields and sample
service calls.

## QoS Determination

ROS Portal determines subscriber QoS by aggregating all publisher endpoints for
a topic, following the same approach as `ros2 topic echo` and Foxglove bridge:

- **Depth:** sum each publisher's history depth, using a minimum of `1` per
  publisher for RMW implementations that report `0`, then clamp to
  `[min_qos_depth, max_qos_depth]`.
- **Reliability:** use `RELIABLE` only when all publishers advertise
  `RELIABLE`. Mixed policies fall back to `BEST_EFFORT` so the subscriber can
  connect to every publisher. Topics matching `best_effort_qos_topics` are
  forced to `BEST_EFFORT`.
- **Durability:** use `TRANSIENT_LOCAL` only when all publishers advertise
  `TRANSIENT_LOCAL`; otherwise use `VOLATILE`.

ROS Portal does not currently register subscription QoS event callbacks.

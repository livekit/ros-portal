# Data Track Schema Design

## Purpose

The ROS 2 LiveKit bridge transports serialized ROS messages in LiveKit data
tracks. CDR bytes are only meaningful when the receiver knows the exact message
definition used to produce them, including every nested message type.

The schema system therefore:

- publishes a self-contained ROS message definition with each data-track type;
- labels data frames as CDR;
- rejects inbound tracks whose schema cannot be verified against the local ROS
  installation; and
- avoids registering the same schema repeatedly.

Schema handling is automatic. There is no schema-specific bridge
configuration.

## System overview

```mermaid
flowchart LR
  subgraph Sender["Sending ROS graph"]
    RP[ROS publisher]
    GS[Generic subscription]
  end

  subgraph SenderBridge["Sending bridge"]
    TF1[TopicForwarder]
    SM1[SchemaManager]
    Publish[Bridge SDK callbacks]
  end

  subgraph LiveKit["LiveKit"]
    Registry[Participant schema blobs]
    Track[Data track]
  end

  subgraph ReceiverBridge["Receiving bridge"]
    Filter[Filter and normalize]
    Resolve[Resolve candidate type]
    SM2[SchemaManager]
    GP[Generic publisher]
  end

  subgraph Receiver["Receiving ROS graph"]
    RS[ROS subscriber]
  end

  RP -->|serialized message| GS
  GS --> TF1
  TF1 --> SM1
  SM1 --> Publish
  Publish -->|defineSchema| Registry
  Publish -->|publishDataTrack| Track
  TF1 -->|raw CDR frame| Track
  Track --> Filter
  Filter --> Resolve
  Resolve --> SM2
  Registry -->|getSchema| SM2
  SM2 -->|accepted CDR| GP
  GP --> RS
```

The bridge applies this path to ordinary data topics. Image topics transported
as video tracks and topics configured as `latched` use different transports and
do not use data-track schemas.

## Schema representation

The renderer accepts a ROS type name in `pkg/msg/Type` form and calls
`rosbag2_cpp::LocalMessageDefinitionSource::get_full_text()`. rosbag2 locates
the installed interface package through the local ament index and returns:

- an encoding, normally `ros2msg` or `ros2idl`; and
- the root definition followed by all recursive dependencies in MCAP message
  definition format.

For a nested message, the schema body has this general shape:

```text
std_msgs/Header header
Pose pose

================================================================================
MSG: geometry_msgs/Pose
Point position
Quaternion orientation

================================================================================
MSG: geometry_msgs/Point
float64 x
float64 y
float64 z

...remaining dependencies...
```

The text is used unchanged. The bridge does not normalize whitespace, remove
comments, reorder dependencies, or replace the body with a digest.

## LiveKit data contract

Each published data track carries four related pieces of information:

```mermaid
flowchart TB
  TrackName["Track name: ROS topic path"]
  SchemaId["Schema ID: ROS type and schema encoding"]
  SchemaBody["Participant schema blob: full recursive definition text"]
  TrackMetadata["Track metadata: name, schema ID, and CDR frame encoding"]
  Frame["Data frame: raw rclcpp SerializedMessage bytes"]

  SchemaId --> SchemaBody
  TrackName --> TrackMetadata
  SchemaId --> TrackMetadata
  TrackMetadata --> Frame
```

For example, a `geometry_msgs/msg/PoseStamped` topic is represented as:

| Field | Value |
| --- | --- |
| Track name | ROS topic path, e.g. `/robot_pose` |
| Schema name | `geometry_msgs/msg/PoseStamped` |
| Schema encoding | `Ros2Msg` when rosbag2 reports `ros2msg` |
| Schema body | Complete MCAP-format definition |
| Frame encoding | `Cdr` |

The outbound encoding mapper uses `Ros2Msg` for `ros2msg`, `Ros2Idl` for
`ros2idl`, and a custom LiveKit schema encoding for other non-empty rosbag2
encodings of at most 25 characters. Empty or longer encoding strings fall back
to `Ros2Msg`. Inbound bridge-to-bridge validation intentionally accepts only
`Ros2Msg` and `Ros2Idl`; a track using a custom encoding is rejected.

The schema body is stored as a participant data blob by LiveKit. Consequently,
the server must enable participant data blobs with
`enable_participant_data_blob: true` or the
`--enable_participant_data_blob` command-line flag.

## Outbound flow

The data-track writer is created lazily when the first ROS message arrives and
is cached per ROS topic after successful creation. Later messages use that
writer directly.

```mermaid
sequenceDiagram
  participant ROS as ROS publisher
  participant TF as TopicForwarder
  participant SM as SchemaManager
  participant Bridge as Ros2LiveKitBridge
  participant RB as rosbag2 definition source
  participant LK as LiveKit participant

  ROS->>TF: SerializedMessage
  TF->>SM: ensureSchemaDefined(type)
  SM->>RB: get_full_text(type)
  RB-->>SM: encoding + full schema text
  SM->>SM: SHA-256(exact schema bytes)

  alt schema ID has not been defined
    SM->>Bridge: define_schema(type, encoding, text)
    Bridge->>LK: defineSchema(...)
    SM->>SM: cache hash
  else cached hash matches
    SM->>SM: reuse schema ID
  else cached hash differs
    SM-->>TF: writer creation failure
  end

  TF->>Bridge: publish_data_track(topic, schema ID)
  Bridge->>LK: publishDataTrack(schema ID, CDR)
  LK-->>TF: data-track writer
  TF->>LK: raw CDR bytes
```

The registration cache key is the rosbag2 encoding, a newline, and the ROS type
(`encoding + "\n" + topic_type`). Its value is the 32-byte binary SHA-256 hash
of the exact schema bytes; hashes are converted to lowercase hexadecimal only
for diagnostics. The cache:

- is protected by a mutex because track writers may be created concurrently;
- records only successful `defineSchema()` calls;
- is scoped to the `SchemaManager` owned by each `TopicForwarder`; and
- rejects different schema text for an existing composite key.

Schema rendering itself is not cached. The renderer creates a local rosbag2
definition source and recomputes the hash for each writer-creation
request, including requests that find an existing registration.

Outbound handling is fail-closed. If the local definition cannot be rendered,
schema registration fails, or LiveKit cannot create the track, no writer is
returned and the message is not sent on an untyped fallback track. Because the
per-topic writer remains unset, the next eligible ROS message retries creation.

## Inbound flow

Before schema work, the forwarder rejects an empty track SID or name, normalizes
the track name to an absolute ROS path (`camera/image` becomes
`/camera/image`), and checks it against the configured incoming-topic patterns.
An eligible inbound track is validated before the bridge creates a ROS
publisher or subscribes to its frames.

```mermaid
sequenceDiagram
  participant LK as Remote LiveKit track
  participant TF as TopicForwarder
  participant SM as SchemaManager
  participant Graph as Local ROS graph
  participant Bridge as Ros2LiveKitBridge
  participant RB as rosbag2 definition source
  participant ROS as Local ROS topic

  LK->>TF: track published
  TF->>TF: validate SID and name; normalize and filter topic
  TF->>Graph: look for an existing type on normalized topic
  alt local endpoint already exists
    Graph-->>TF: local ROS type
  else no local endpoint yet
    TF->>TF: use schema name as candidate type
  end
  TF->>TF: check CDR and schema presence
  TF->>SM: validateInboundSchema(schema ID, publisher identity, type)
  SM->>SM: check schema encoding and type name
  SM->>Bridge: get_schema(schema ID, publisher identity)
  Bridge->>LK: getSchema(...)
  LK-->>SM: remote schema text
  SM->>RB: get_full_text(local ROS type)
  RB-->>SM: local encoding + schema text
  SM->>SM: compare encoding, SHA-256, and exact bytes

  alt all checks pass
    TF->>TF: map final ROS topic name
    TF->>ROS: create generic publisher with QoS(10)
    TF->>LK: subscribe to data frames
    LK->>ROS: republish each raw CDR frame
  else any check fails
    TF-->>TF: log rejection; create no publisher
  end
```

If the normalized topic already exists on the local ROS graph, its type takes
precedence so conflicting remote metadata is rejected. When no local endpoint
exists yet, the schema name supplies the candidate type. That candidate is not
trusted by itself: the bridge must render the named interface from its local
installation and complete exact schema validation before creating a publisher.

If the graph reports multiple types and one matches the advertised schema, the
bridge selects that type. Otherwise it warns and uses the first graph type,
which causes conflicting schema metadata to fail validation.

After validation, the final ROS topic name is mapped from the LiveKit track
name. For topics configured with `preserve_id: true`, the sanitized publisher
identity is prepended. This mapping changes only the publication path; schema
type resolution and validation still use the normalized track topic.

Validation accepts a track only when all of these checks pass:

1. Frame-encoding metadata is present.
2. The advertised frame encoding is `Cdr`.
3. A schema ID is present.
4. The schema encoding is `Ros2Msg` or `Ros2Idl`.
5. The schema name exactly equals the resolved candidate ROS type. An existing
   local graph type takes precedence when one is available.
6. The remote participant's schema blob can be retrieved.
7. The same ROS type can be rendered from the local installation.
8. Remote and local schema encodings match.
9. SHA-256 hashes and exact schema bytes both match.

Checking both the digest and the text makes the intended byte-exact contract
explicit. The digest is also included in mismatch diagnostics, while direct
comparison remains the final authority.

```mermaid
flowchart TD
  Start[Remote data track] --> Eligible{"Valid name and allowed incoming topic?"}
  Eligible -- No --> Ignore[Ignore track]
  Eligible -- Yes --> Metadata{"CDR and supported schema metadata?"}
  Metadata -- No --> Reject[Reject track]
  Metadata -- Yes --> Graph{"Local endpoint exists?"}
  Graph -- Yes --> Type{"Schema name matches a local ROS type?"}
  Type -- No --> Reject
  Type -- Yes --> Fetch{"Remote schema retrieved and local schema rendered?"}
  Graph -- No --> Fetch
  Fetch -- No --> Reject
  Fetch -- Yes --> Match{"Encoding, SHA-256, and exact bytes match?"}
  Match -- No --> Reject
  Match -- Yes --> Accept["Create ROS publisher and forward CDR frames"]
```

### Arrival-order guarantee

Schema verification does not depend on an application publisher or subscriber
already being present on the receiving graph. The forwarder is also installed
before the bridge connects to LiveKit, because connection can emit publication
events for tracks that already exist. These orders are supported:

1. a local endpoint advertises the topic, then the remote track arrives;
2. the bridge is connected, the remote track arrives, then a local endpoint
   joins; and
3. the remote track exists first, then the bridge connects and validates it,
   then a local endpoint joins.

When no endpoint exists, the schema ID provides the type name while the locally
installed interface definition provides the trust anchor. The bridge publisher
is therefore ready for a late subscriber without weakening exact-text
validation. A later endpoint that uses a different ROS type remains incompatible
at the ROS graph level.

## Compatibility implications

Exact-text comparison is intentionally stricter than the ROS RIHS01 type hash.
Comments, whitespace, constants, defaults, dependency ordering, and all nested
definitions affect compatibility.

This design assumes:

- sender and receiver have compatible interface packages installed;
- rosbag2 renders those packages identically on both sides;
- the bridge is built against the pinned schema-capable LiveKit C++ SDK release
  or a compatible local install; and
- the LiveKit server supports and enables participant data blobs.

Current SDK and server setup requirements are maintained in the repository
[development guide](../development.md#livekit-sdk).

The bridge does not query endpoint type hashes. A semantically compatible but
textually different definition is rejected rather than risking CDR
misinterpretation.

## Failure behavior

Failures stop at the boundary where they are detected:

- rendering or registration failure prevents an outbound data track;
- missing or unsupported metadata rejects an inbound track;
- schema retrieval failure rejects an inbound track;
- local definition lookup failure rejects an inbound track; and
- any encoding or exact-text mismatch rejects an inbound track.

Rejected inbound tracks never create a local generic publisher, so
unverified CDR bytes cannot enter the local ROS graph.

## Implementation map

- Schema rendering, registration state, and inbound validation:
  [`schema_manager.cpp`](../../src/ros2_livekit_bridge/src/schema_manager.cpp) and
  [`schema_manager.hpp`](../../src/ros2_livekit_bridge/include/ros2_livekit_bridge/schema_manager.hpp)
- LiveKit SDK callback adapters:
  [`ros2_livekit_bridge.cpp`](../../src/ros2_livekit_bridge/src/ros2_livekit_bridge.cpp) and
  [`ros2_livekit_bridge.hpp`](../../src/ros2_livekit_bridge/include/ros2_livekit_bridge/ros2_livekit_bridge.hpp)
- Inbound validation and CDR forwarding:
  [`topic_forwarder.cpp`](../../src/ros2_livekit_bridge/src/topic_forwarder.cpp) and
  [`topic_forwarder.hpp`](../../src/ros2_livekit_bridge/include/ros2_livekit_bridge/topic_forwarder.hpp)
- Inbound topic normalization and identity-preserving mapping:
  [`ros_utils.cpp`](../../src/ros2_livekit_bridge/src/utils/ros_utils.cpp)
- Unit coverage:
  [`schema_manager_test.cpp`](../../src/ros2_livekit_bridge/test/unit/schema_manager_test.cpp) and
  [`topic_forwarder_test.cpp`](../../src/ros2_livekit_bridge/test/unit/topic_forwarder_test.cpp)
- End-to-end acceptance and rejection:
  [`bridge_e2e_test.cpp`](../../src/ros2_livekit_bridge/test/integration/bridge_e2e_test.cpp)

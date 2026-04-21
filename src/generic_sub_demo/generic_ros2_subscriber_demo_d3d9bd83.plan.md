---
name: Generic ROS2 Subscriber Demo
overview: Build a standalone ROS 2 package that uses `rclcpp::GenericSubscription` and `rclcpp::SerializedMessage` to subscribe to arbitrary topics from a YAML config, capturing raw CDR bytes without any compile-time message type knowledge -- eliminating the if/else chain pattern in the existing bridge.
todos:
  - id: create-package-scaffold
    content: "Create `src/generic_sub_demo/` with `CMakeLists.txt` and `package.xml` (deps: rclcpp, yaml_cpp_vendor only)"
    status: completed
  - id: write-yaml-config
    content: Create `config/topics.yaml` with sample topic entries (name, type, qos_depth)
    status: completed
  - id: implement-node
    content: "Write `src/generic_sub_node.cpp`: YAML parsing, generic subscription loop, SerializedMessage byte capture callback"
    status: completed
isProject: false
---

# Generic ROS 2 Subscriber Demo

## Feasibility Assessment

All three requirements are achievable with standard rclcpp APIs (available since Humble, confirmed in Jazzy):

**1. Runtime type resolution from string** -- Yes. `rclcpp::Node::create_generic_subscription()` accepts the message type as a plain string (e.g., `"std_msgs/msg/String"`). The RMW layer resolves the type at runtime via the type support system; no shared-object lookup code is needed on your side. This is the same mechanism `rosbag2_transport::Recorder` and `foxglove_bridge` use internally.

**2. YAML-driven subscriber creation** -- Yes. Read topic entries with `yaml-cpp`, loop over them, call `create_generic_subscription()` for each. Zero `#include`s of specific message packages required.

**3. Raw byte payload in a vector** -- Yes. The generic callback receives `std::shared_ptr<rclcpp::SerializedMessage>`. Internally this wraps `rcl_serialized_message_t` with a `.buffer` (uint8_t*) and `.buffer_length`. These are CDR-serialized bytes -- the same format rosbag2 writes into MCAP files. Copy into a `std::vector<uint8_t>` trivially.

### Key API

```cpp
auto sub = this->create_generic_subscription(
    topic_name,             // "/odom"
    topic_type,             // "nav_msgs/msg/Odometry"
    qos,
    [](std::shared_ptr<rclcpp::SerializedMessage> msg) {
        auto & rcl_msg = msg->get_rcl_serialized_message();
        std::vector<uint8_t> bytes(rcl_msg.buffer, rcl_msg.buffer + rcl_msg.buffer_length);
        // bytes now contains the raw CDR payload
    }
);
```

No template parameters, no compile-time type knowledge, no if/else chains.

## What This Replaces

The existing bridge has a 13-branch if/else chain in [`ros2_livekit_bridge.cpp:354-391`](src/ros2_livekit_bridge/src/ros2_livekit_bridge.cpp), 12 explicit template instantiations, and `find_package` / `#include` for every supported message package. The generic approach eliminates all of that for the data-track path.

## Package Structure

New standalone package at `src/generic_sub_demo/`:

```
src/generic_sub_demo/
  CMakeLists.txt
  package.xml
  config/topics.yaml
  src/generic_sub_node.cpp
```

### `config/topics.yaml`

```yaml
topics:
  - name: "/odom"
    type: "nav_msgs/msg/Odometry"
    qos_depth: 10
  - name: "/scan"
    type: "sensor_msgs/msg/LaserScan"
    qos_depth: 5
  - name: "/chatter"
    type: "std_msgs/msg/String"
    qos_depth: 10
```

### `src/generic_sub_node.cpp`

Single-file ROS 2 node (~120 lines):

- Declares a `config_file` parameter pointing to the YAML
- Parses the YAML with `yaml-cpp`
- For each entry, calls `create_generic_subscription(name, type, QoS{depth}, callback)`
- The callback copies `SerializedMessage` bytes into a `std::vector<uint8_t>` and logs the topic + byte count
- Stores subscriptions in a `std::vector<rclcpp::GenericSubscription::SharedPtr>`

### `CMakeLists.txt`

Minimal ament_cmake package. Only dependencies: `rclcpp` and `yaml_cpp_vendor` (the ROS 2-vendored yaml-cpp). No message packages needed.

### `package.xml`

`build_depend` and `exec_depend` on `rclcpp` and `yaml_cpp_vendor` only.

## How to Build and Run

```bash
colcon build --packages-select generic_sub_demo
source install/setup.bash
ros2 run generic_sub_demo generic_sub_node --ros-args -p config_file:=$(pwd)/src/generic_sub_demo/config/topics.yaml
```

Then publish on any of the configured topics in another terminal to see serialized byte captures logged.

## Design Note: Image Topics

`GenericSubscription` gives you raw CDR bytes for all message types, including `sensor_msgs/msg/Image`. If the eventual bridge integration still needs to decode images into RGBA pixels for LiveKit video tracks, that specific path will still require a typed subscription (or CDR deserialization). But for the data-track path (Foxglove protobuf or raw forwarding), generic subscriptions eliminate the entire type dispatch layer.

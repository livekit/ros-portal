# ros2_medkit patch

`ros2_medkit_serialization` vendors dynmsg helpers that assume `uint8`
sequences are `std::vector<uint8_t>`. Newer ROS distributions store those
sequences in `rosidl::Buffer` instead, so a direct vector cast crashes or
corrupts data when rendering or parsing YAML for types like
`std_msgs/msg/UInt8MultiArray`.

`0001-support-rosidl-buffer-uint8-sequences.patch` switches the uint8 sequence
paths to the introspection resize/fetch/assign callbacks, which work with
either container. Drop this once upstream medkit supports `rosidl::Buffer`
natively and the pin can move past that fix.

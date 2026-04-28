# Agent Guidance

This repository is a ROS2 workspace. Keep changes small, idiomatic, and aligned
with existing package structure.

## Build And Dependencies

- Prefer `ament_cmake_auto` and the `ament_auto_*` helpers for ROS2 CMake
  packages.
- Keep ROS dependencies simple and explicit. Avoid adding broad dependency
  surfaces when a narrower message or utility package is enough.

## Architecture

- Design library code around ROS2-facing interfaces, but keep the library
  implementation independently compilable from ROS2.
- Put ROS2 integration in thin wrapper nodes that instantiate the library,
  translate parameters/messages, and plug the library into the ROS2 graph.
- Keep public APIs small and focused. Add abstractions only when they simplify
  real usage or match an existing pattern.
- Design new code with offline testing in mind. Prefer deterministic seams for
  IO, clocks, networking, and ROS graph interactions.
- New functionality should be able to run in simulation without hardware-only
  assumptions.
- Make code as performant as practical. Avoid unnecessary copies, allocations,
  blocking work, and ROS graph churn on hot paths.

## Documentation

- Keep documentation clean, concise, and practical. Explain the supported path,
  key configuration, and verification steps without duplicating implementation
  details.

## Style

- Follow ROS2 formatting and linting standards.
- Add the LiveKit copyright header with the correct year to new code files.


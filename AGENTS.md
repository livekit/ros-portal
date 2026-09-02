# Agent Guidance

This repository is a ROS2 workspace. Keep changes small, idiomatic, and aligned
with existing package structure.

## Shared C++ Baseline

Follow `src/externals/cpp-tools/AGENTS.md` for shared C++ rules and this file
for SDK-specific guidance.

Before C++ work, verify the shared guidance and root `.clang-format` /
`.clang-tidy` symlinks are present. If not, flag it and recommend
`vcs import src/externals < external.repos` or
`./src/externals/cpp-tools/install.sh --repo-root .` as appropriate. Never use
`--force` without approval or claim tooling verification while these
prerequisites are missing. Project-specific commands are documented in
`docs/tools.md`.

## Build And Dependencies

- Prefer `ament_cmake_auto` and the `ament_auto_*` helpers for ROS2 CMake
  packages.
- Use `medkit_target_dependencies` for dependency linking that must work across
  all supported ROS distributions. It delegates to `ament_target_dependencies`
  where available and uses exported CMake targets on Lyrical, where the ament
  helper has been removed. Include `ROS2MedkitCompat` before using it.
- Keep ROS dependencies simple and explicit. Avoid adding broad dependency
  surfaces when a narrower message or utility package is enough.

## Testing And Verification

- Prefer running ROS build and test commands inside the devcontainer when the
  host shell lacks ROS tools. Use the devcontainer CLI directly, for example:
  `devcontainer exec --workspace-folder . bash -lc 'source "/opt/ros/${ROS_DISTRO}/setup.bash" && source install/setup.bash && colcon test --packages-select ros_portal --ctest-args -R ros_portal_unit_tests'`.
  If the container is not running, start it with
  `devcontainer up --workspace-folder .`.
- Treat behavior changes as test-impacting changes. If functionality changes,
 add or update tests in the nearest existing test target (unit first, then
 integration/end-to-end as needed).
- When tests already exist for the affected area, run them after making
 functional changes. Do not skip existing relevant tests.
- Prefer targeted test commands during iteration. After any functional change
 to `ros_portal`, run both the unit and integration suites before
 finalizing; a passing unit suite alone is not sufficient:
  - `colcon test --packages-select ros_portal --ctest-args -R ros_portal_unit_tests`
  - `colcon test --packages-select ros_portal --ctest-args -R ros_portal_integration_tests`
  - `colcon test-result --verbose`
- Before treating integration credentials as unavailable, source
  `scripts/set-test-tokens.sh` in the same shell that runs the tests.
  For example:
  `source scripts/set-test-tokens.sh && colcon test --packages-select ros_portal --ctest-args -R ros_portal_integration_tests`.
- If an integration test requires external services or credentials, keep the
 test deterministic and document/emit the exact environment assumptions.
- If tests cannot be run in the current environment, explicitly state what was
  not run and why, including the concrete credential-helper or service failure,
  and provide the exact command(s) to run.
- After C++ changes, run `./scripts/clang-tidy.sh` (optionally pass specific
  source files). The wrapper fails on warnings. This needs
  `compile_commands.json` from a compile-commands-enabled build; see
  `docs/development.md`.

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
- Avoid duplicating code by utilizing functions and classes with well-defined interfaces.
- Document new functions or signature updates in the header with proper `///` style Doxygen comments.

## Documentation

- Keep documentation clean, concise, and practical. Explain the supported path,
  key configuration, and verification steps without duplicating implementation
  details.
- If configuration fields are added, updated, or removed, update the documentation in `docs/configuration.md`. If reviewing work, add a task to update the corresponding sections of the external documentation.
- If diagnostics fields are added, updated, or removed, update the documentation in `docs/diagnostics.md`. If reviewing work, add a task to update the corresponding sections of the external web/ repo documentation.

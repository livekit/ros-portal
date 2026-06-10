# ros2_livekit_bridge_config

Schema-driven YAML configuration parser and config type definitions for
`ros2_livekit_bridge`.

The JSON schema is the source of truth for the C++ config structs used by the
bridge. `BridgeConfig`, its nested structs, enums, and parser are generated from
`schema/ros_livekit_bridge_config.schema.json`; the main bridge package depends
on this package and consumes those generated types directly.

## Package Layout

- `schema/ros_livekit_bridge_config.schema.json`: source of truth for the YAML
  config structure and the generated C++ structs used by the bridge.
- `scripts/generate_config_parser.py`: reads the schema and builds a small model
  of the config types, fields, enums, and parser functions.
- `scripts/templates/config_parser.hpp.j2`: Jinja2 template for the public C++
  API consumed by the bridge, including `BridgeConfig`, nested config structs,
  enums, and `ConfigParser`.
- `scripts/templates/config_parser.cpp.j2`: Jinja2 template for the parser
  implementation and schema-specific validation logic.
- `include/ros2_livekit_bridge_config/config/error.hpp`: public exception type
  thrown when config parsing or validation fails.
- `src/config/utils.hpp` and `src/config/utils.cpp`: schema-agnostic YAML
  helpers used by the generated parser.
- `test/`: unit tests for generated parser behavior and the shared YAML
  utilities.

## Code Generation

CMake runs `scripts/generate_config_parser.py` during the build. The generator
loads `schema/ros_livekit_bridge_config.schema.json`, renders the `.hpp.j2` and
`.cpp.j2` templates, and writes generated files under the build directory:

- `generated/config_parser/include/ros2_livekit_bridge_config/config/config_parser.hpp`
- `generated/config_parser/src/config/config_parser.cpp`

The generated header is installed with the package, so downstream code includes:

```cpp
#include "ros2_livekit_bridge_config/config/config_parser.hpp"
```

When adding or changing config fields, update the JSON schema first. The structs
used by the bridge and the parser validation code are regenerated from that
schema on the next build. Template changes should only be needed when changing
the generated C++ shape or supporting a new schema feature.

## YAML Utilities

`src/config/utils.hpp` and `src/config/utils.cpp` keep reusable yaml-cpp
validation helpers out of the generated code. They handle common operations such
as:

- building readable field paths like `$.ros_topics`
- adding line and column context to errors
- checking map and sequence nodes
- converting scalar values
- rejecting unknown fields

These helpers do not know about `BridgeConfig`; schema-specific behavior belongs
in the generator/templates.

## Tests

`config_parser_test` verifies the generated parser against representative YAML
inputs. `config_utils_test` covers the schema-independent YAML helper behavior.

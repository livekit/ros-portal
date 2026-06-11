#!/usr/bin/env python3
#
# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# flake8: noqa

"""Generate config_parser.hpp/cpp from ros2_livekit_bridge_config.schema.json."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
from typing import Any

from jinja2 import Environment, FileSystemLoader, StrictUndefined


HEADER_PROLOGUE = """/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// This file is generated from schema/ros2_livekit_bridge_config.schema.json.
// Do not edit by hand.
"""


@dataclass(frozen=True)
class EnumSpec:
    schema_name: str
    cpp_name: str
    values: tuple[str, ...]


@dataclass(frozen=True)
class TypeInfo:
    cpp_type: str
    kind: str
    schema: dict[str, Any]
    ref_name: str | None = None
    enum_schema_name: str | None = None
    array_item: "TypeInfo | None" = None


@dataclass
class FieldSpec:
    yaml_name: str
    member_name: str
    const_name: str
    type_info: TypeInfo
    required: bool
    member_type: str
    default_value: Any | None = None


@dataclass
class ObjectSpec:
    schema_name: str
    cpp_name: str
    parse_name: str
    fields: list[FieldSpec] = field(default_factory=list)


class SchemaModel:
    def __init__(self, schema: dict[str, Any]) -> None:
        self.schema = schema
        self.defs = schema.get("$defs", {})
        self.enum_defs = self._collect_enum_defs()
        self.enum_canonical = self._canonicalize_enum_defs()
        self.canonical_enums = self._collect_canonical_enums()
        self.object_specs: list[ObjectSpec] = []
        self.root_key = self._find_root_config_key()
        self.root_spec = self._build_object_spec(
            self.schema["properties"][self.root_key],
            schema_name=self.root_key,
            cpp_name="BridgeConfig",
            parse_name="parseBridgeConfig",
            depth=1)
        self._build_def_object_specs()

    def _collect_enum_defs(self) -> dict[str, EnumSpec]:
        enums: dict[str, EnumSpec] = {}
        for name, definition in self.defs.items():
            if "enum" not in definition:
                continue
            enums[name] = EnumSpec(
                schema_name=name,
                cpp_name=pascal_case(name),
                values=tuple(str(value) for value in definition["enum"]))
        return enums

    def _canonicalize_enum_defs(self) -> dict[str, str]:
        canonical: dict[str, str] = {}
        for name, enum in self.enum_defs.items():
            enum_values = set(enum.values)
            supersets = [
                candidate
                for candidate, candidate_enum in self.enum_defs.items()
                if candidate != name and enum_values < set(candidate_enum.values)
            ]
            if supersets:
                supersets.sort(key=lambda candidate: len(self.enum_defs[candidate].values))
                canonical[name] = supersets[0]
            else:
                canonical[name] = name
        return canonical

    def _collect_canonical_enums(self) -> list[EnumSpec]:
        names = []
        for canonical_name in self.enum_canonical.values():
            if canonical_name not in names:
                names.append(canonical_name)
        return [self.enum_defs[name] for name in names]

    def _find_root_config_key(self) -> str:
        properties = self.schema.get("properties", {})
        if len(properties) != 1:
            fail("expected root schema to contain exactly one config object property")
        key, value = next(iter(properties.items()))
        if resolve_type(value, self.defs).get("type") != "object":
            fail(f"expected root property {key!r} to be an object")
        return key

    def _build_def_object_specs(self) -> None:
        by_name: dict[str, ObjectSpec] = {}
        for name, definition in self.defs.items():
            if resolve_type(definition, self.defs).get("type") != "object":
                continue
            by_name[name] = self._build_object_spec(
                definition,
                schema_name=name,
                cpp_name=pascal_case(name),
                parse_name=f"parse{pascal_case(name)}",
                depth=2)

        pending = dict(by_name)
        emitted: set[str] = set()
        while pending:
            progressed = False
            for name, spec in list(pending.items()):
                deps = object_dependencies(spec)
                if deps <= emitted:
                    self.object_specs.append(spec)
                    emitted.add(name)
                    del pending[name]
                    progressed = True
            if not progressed:
                cycle = ", ".join(sorted(pending))
                fail(f"unsupported cycle in object schema definitions: {cycle}")

    def _build_object_spec(
            self,
            schema: dict[str, Any],
            *,
            schema_name: str,
            cpp_name: str,
            parse_name: str,
            depth: int) -> ObjectSpec:
        schema = resolve_type(schema, self.defs)
        if schema.get("type") != "object":
            fail(f"expected {schema_name} to be an object schema")

        required = set(schema.get("required", []))
        spec = ObjectSpec(schema_name=schema_name, cpp_name=cpp_name, parse_name=parse_name)
        for yaml_name, property_schema in schema.get("properties", {}).items():
            type_info = self.type_info(property_schema)
            member_type = member_type_for(
                type_info,
                required=yaml_name in required,
                parent_depth=depth,
                has_default="default" in property_schema)
            spec.fields.append(FieldSpec(
                yaml_name=yaml_name,
                member_name=snake_case(yaml_name),
                const_name=f"k{pascal_case(yaml_name)}",
                type_info=type_info,
                required=yaml_name in required,
                member_type=member_type,
                default_value=property_schema.get("default")))
        return spec

    def type_info(self, schema: dict[str, Any]) -> TypeInfo:
        ref = schema.get("$ref")
        if ref:
            name = ref_name(ref)
            definition = resolve_ref(ref, self.defs)
            resolved = resolve_type(definition, self.defs)
            if name in self.enum_defs:
                canonical_name = self.enum_canonical[name]
                return TypeInfo(
                    cpp_type=self.enum_defs[canonical_name].cpp_name,
                    kind="enum",
                    schema=definition,
                    ref_name=name,
                    enum_schema_name=name)
            if resolved.get("type") == "object":
                return TypeInfo(
                    cpp_type=pascal_case(name),
                    kind="object",
                    schema=resolved,
                    ref_name=name)
            if resolved.get("type") in {"string", "integer", "number", "boolean"}:
                return TypeInfo(
                    cpp_type=cpp_scalar_type(resolved),
                    kind=resolved["type"],
                    schema=resolved,
                    ref_name=name)
            fail(f"unsupported $ref target {ref!r}")

        resolved = resolve_type(schema, self.defs)
        if "const" in resolved:
            const_value = resolved["const"]
            if not isinstance(const_value, str):
                fail("only string const schema values are supported")
            return TypeInfo(cpp_type="std::string", kind="const_string", schema=resolved)
        if "enum" in resolved:
            fail("inline enum schemas are not supported; define them under $defs")
        schema_type = resolved.get("type")
        if schema_type == "array":
            item = self.type_info(resolved["items"])
            return TypeInfo(
                cpp_type=f"std::vector<{item.cpp_type}>",
                kind="array",
                schema=resolved,
                array_item=item)
        if schema_type == "object":
            fail("inline object schemas are not supported; define them under $defs")
        if schema_type in {"string", "integer", "number", "boolean"}:
            return TypeInfo(cpp_type=cpp_scalar_type(resolved), kind=schema_type, schema=resolved)
        fail(f"unsupported schema type {schema_type!r}")


def cpp_scalar_type(schema: dict[str, Any]) -> str:
    schema_type = schema.get("type")
    if schema_type == "string":
        return "std::string"
    if schema_type == "integer":
        return "int"
    if schema_type == "number":
        return "double"
    if schema_type == "boolean":
        return "bool"
    fail(f"unsupported scalar schema type {schema_type!r}")


def member_type_for(
    type_info: TypeInfo,
    *,
    required: bool,
    parent_depth: int,
    has_default: bool) -> str:
    if required or has_default or type_info.kind == "array":
        return type_info.cpp_type
    if type_info.kind == "object" and parent_depth == 1:
        return type_info.cpp_type
    return f"std::optional<{type_info.cpp_type}>"


def object_dependencies(spec: ObjectSpec) -> set[str]:
    deps = set()
    for field_spec in spec.fields:
        type_info = field_spec.type_info
        if type_info.kind == "object" and type_info.ref_name:
            deps.add(type_info.ref_name)
        if (
                type_info.kind == "array" and
                type_info.array_item and
                type_info.array_item.kind == "object" and
                type_info.array_item.ref_name):
            deps.add(type_info.array_item.ref_name)
    deps.discard(spec.schema_name)
    return deps


def make_template_context(model: SchemaModel) -> dict[str, Any]:
    return {
        "allowed_field_set": allowed_field_set,
        "all_specs": [*model.object_specs, model.root_spec],
        "const_values": collect_const_values(model),
        "const_value_name": const_value_name,
        "cpp_bool": cpp_bool,
        "default_initializer": default_initializer,
        "enum_allowed_values": lambda type_info: enum_allowed_values(type_info, model),
        "enum_constant_name": enum_constant_name,
        "enum_cpp_name": lambda type_info: model.enum_defs[
            model.enum_canonical[type_info.enum_schema_name]].cpp_name,
        "field_constants": collect_field_constants(model),
        "guard": "ROS2_LIVEKIT_BRIDGE_CONFIG__CONFIG__CONFIG_PARSER_HPP_",
        "integer_expected": integer_expected,
        "integer_minimum_args": integer_minimum_args,
        "model": model,
        "number_expected": number_expected,
        "number_minimum_args": number_minimum_args,
        "prologue": HEADER_PROLOGUE.rstrip(),
        "requires_nonempty": requires_nonempty,
        "root_const": f"k{pascal_case(model.root_key)}",
    }


def render_template(template_dir: Path, template_name: str, model: SchemaModel) -> str:
    env = Environment(
        loader=FileSystemLoader(template_dir),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True)
    env.filters["enum_value_name"] = enum_value_name
    return env.get_template(template_name).render(**make_template_context(model))


def collect_field_constants(model: SchemaModel) -> list[tuple[str, str]]:
    fields: list[tuple[str, str]] = [(model.root_key, f"k{pascal_case(model.root_key)}")]
    seen = {model.root_key}
    for spec in [*model.object_specs, model.root_spec]:
        for field_spec in spec.fields:
            if field_spec.yaml_name in seen:
                continue
            seen.add(field_spec.yaml_name)
            fields.append((field_spec.yaml_name, field_spec.const_name))
    return fields


def collect_const_values(model: SchemaModel) -> list[tuple[str, str]]:
    values: list[tuple[str, str]] = []
    for spec in [*model.object_specs, model.root_spec]:
        for field_spec in spec.fields:
            if field_spec.type_info.kind != "const_string":
                continue
            value = str(field_spec.type_info.schema["const"])
            values.append((const_value_name(spec.cpp_name, field_spec.yaml_name), value))
    return values


def const_value_name(spec_name: str, field_name: str) -> str:
    if field_name == "version":
        return "kConfigVersion"
    return f"k{spec_name}{pascal_case(field_name)}"


def enum_constant_name(enum: EnumSpec, value: str) -> str:
    return f"k{enum.cpp_name}{enum_value_name(value)}"


def allowed_field_set(spec: ObjectSpec) -> str:
    if not spec.fields:
        return "{}"
    values = ", ".join(f"std::string({field_spec.const_name})" for field_spec in spec.fields)
    return "{" + values + "}"


def enum_allowed_values(type_info: TypeInfo, model: SchemaModel) -> str:
    if not type_info.enum_schema_name:
        fail("enum type info missing schema name")
    values = model.enum_defs[type_info.enum_schema_name].values
    canonical = model.enum_defs[model.enum_canonical[type_info.enum_schema_name]]
    constants = ", ".join(enum_constant_name(canonical, value) for value in values)
    return "{" + constants + "}"


def requires_nonempty(schema: dict[str, Any]) -> bool:
    return int(schema.get("minLength", 0)) > 0


def integer_expected(schema: dict[str, Any]) -> str:
    minimum = schema.get("minimum")
    if minimum is None:
        return "integer"
    if int(minimum) == 1:
        return "positive integer"
    return f"integer >= {int(minimum)}"


def integer_minimum_args(schema: dict[str, Any]) -> str:
    minimum = schema.get("minimum")
    if minimum is None:
        return f"false, 0, \"{integer_expected(schema)}\""
    return f"true, {int(minimum)}, \"{integer_expected(schema)}\""


def number_expected(schema: dict[str, Any]) -> str:
    minimum = schema.get("minimum")
    if minimum is None:
        return "number"
    return f"number >= {float(minimum):g}"


def number_minimum_args(schema: dict[str, Any]) -> str:
    minimum = schema.get("minimum")
    if minimum is None:
        return f"false, 0.0, \"{number_expected(schema)}\""
    return f"true, {float(minimum):g}, \"{number_expected(schema)}\""


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def default_initializer(field_spec: FieldSpec) -> str:
    if field_spec.default_value is None:
        return ""
    return f" = {cpp_default_literal(field_spec.type_info, field_spec.default_value)}"


def cpp_default_literal(type_info: TypeInfo, value: Any) -> str:
    if type_info.kind in {"integer", "number"}:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            fail(f"default for {type_info.cpp_type} must be numeric")
        return str(value)
    if type_info.kind == "boolean":
        if not isinstance(value, bool):
            fail("default for bool must be boolean")
        return cpp_bool(value)
    if type_info.kind in {"string", "const_string"}:
        if not isinstance(value, str):
            fail("default for string must be a string")
        return f"\"{cpp_string_literal(value)}\""
    if type_info.kind == "enum":
        if not isinstance(value, str):
            fail("default for enum must be a string")
        return f"{type_info.cpp_type}::{enum_value_name(value)}"
    fail(f"defaults are not supported for {type_info.kind} fields")


def cpp_string_literal(value: str) -> str:
    return (
        value
        .replace("\\", "\\\\")
        .replace("\"", "\\\"")
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t"))


def resolve_type(schema: dict[str, Any], defs: dict[str, Any]) -> dict[str, Any]:
    if "$ref" in schema:
        return resolve_type(resolve_ref(schema["$ref"], defs), defs)
    return schema


def resolve_ref(ref: str, defs: dict[str, Any]) -> dict[str, Any]:
    name = ref_name(ref)
    if name not in defs:
        fail(f"unsupported or unknown schema reference {ref!r}")
    return defs[name]


def ref_name(ref: str) -> str:
    prefix = "#/$defs/"
    if not ref.startswith(prefix):
        fail(f"only local $defs references are supported, got {ref!r}")
    return ref[len(prefix):]


def pascal_case(name: str) -> str:
    words = re.findall(r"[A-Za-z0-9]+", name)
    if not words:
        fail(f"cannot derive C++ identifier from {name!r}")
    return "".join(word[:1].upper() + word[1:] for word in words)


def snake_case(name: str) -> str:
    name = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    name = re.sub(r"[^A-Za-z0-9]+", "_", name)
    return name.lower().strip("_")


def enum_value_name(value: str) -> str:
    name = pascal_case(value)
    if name[0].isdigit():
        name = f"Value{name}"
    return name


def fail(message: str) -> None:
    raise RuntimeError(f"config parser generation failed: {message}")


def write_if_changed(path: Path, contents: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == contents:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument(
        "--template-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "templates")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    model = SchemaModel(schema)
    write_if_changed(
        args.header,
        render_template(args.template_dir, "config_parser.hpp.j2", model))
    write_if_changed(
        args.source,
        render_template(args.template_dir, "config_parser.cpp.j2", model))


if __name__ == "__main__":
    main()

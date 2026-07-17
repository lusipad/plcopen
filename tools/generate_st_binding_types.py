#!/usr/bin/env python3
"""Generate the non-builtin ST type universe from its checked-in authority."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass, field


ROOT = pathlib.Path(__file__).resolve().parents[1]
AUTHORITY = ROOT / "doc/compliance/st-binding-types.yml"
PIN_CATALOG = ROOT / "doc/compliance/generated/st-binding-fb-pins.yml"
OUTPUT = ROOT / "core/st/generated/st_binding_types.h"

BUILTINS = {
    "BOOL": (1, 1, 1), "SINT": (2, 1, 1), "INT": (3, 2, 2),
    "DINT": (4, 4, 4), "LINT": (5, 8, 8), "USINT": (6, 1, 1),
    "UINT": (7, 2, 2), "UDINT": (8, 4, 4), "ULINT": (9, 8, 8),
    "REAL": (10, 4, 4), "LREAL": (11, 8, 8), "TIME": (12, 8, 8),
    "BYTE": (13, 1, 1), "WORD": (14, 2, 2), "DWORD": (15, 4, 4),
    "LWORD": (16, 8, 8), "DATE": (17, 8, 8), "TOD": (18, 8, 8),
    "DT": (19, 8, 8),
}
BUILTIN_CPP = {
    "BOOL": "builtin::bool_", "SINT": "builtin::sint", "INT": "builtin::int_",
    "DINT": "builtin::dint", "LINT": "builtin::lint", "USINT": "builtin::usint",
    "UINT": "builtin::uint_", "UDINT": "builtin::udint", "ULINT": "builtin::ulint",
    "REAL": "builtin::real", "LREAL": "builtin::lreal", "TIME": "builtin::time",
    "BYTE": "builtin::byte_", "WORD": "builtin::word", "DWORD": "builtin::dword",
    "LWORD": "builtin::lword", "DATE": "builtin::date", "TOD": "builtin::tod",
    "DT": "builtin::dt",
}
KIND_CODE = {"enum": 1, "array": 3, "struct": 4, "ref": 10}
PUBLIC_KINDS = {"enum", "struct", "fixed-array", "ref", "host-ref", "span"}
CODECS = {
    "enum-map", "fields", "fixed-array", "counted-array",
    "binding-ref", "ref-alias", "span-registry", "profile-sequence",
    "tagged-reference",
}


@dataclass
class Desc:
    name: str
    kind: str
    type_id: int
    size: int
    alignment: int
    enum_base: int = 0
    enum_items: list[dict] = field(default_factory=list)
    element: int = 0
    bounds: list[tuple[int, int, int, int]] = field(default_factory=list)
    fields: list[tuple[str, int, int]] = field(default_factory=list)
    target: int = 0


def cpp_quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def identifier(name: str) -> str:
    return name.lower()


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def read_authority() -> tuple[dict, list[dict]]:
    lines = AUTHORITY.read_text(encoding="utf-8").splitlines()
    if "schema: plcopen-st-binding-types-v1" not in lines:
        raise RuntimeError("unexpected ST binding type authority schema")
    abi_lines = [line for line in lines if line.startswith("abi: ")]
    if len(abi_lines) != 1:
        raise RuntimeError("expected exactly one ABI row")
    abi = json.loads(abi_lines[0][5:])
    types = [json.loads(line[4:]) for line in lines if line.startswith("  - {")]
    if len(types) != 49:
        raise RuntimeError(f"expected 49 non-builtin types, got {len(types)}")
    names: set[str] = set()
    for item in types:
        name = item.get("name", "")
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or name in names:
            raise RuntimeError(f"invalid or duplicate type name {name!r}")
        names.add(name)
        if item.get("kind") not in PUBLIC_KINDS:
            raise RuntimeError(f"{name}: invalid kind {item.get('kind')}")
        if item.get("codec") not in CODECS:
            raise RuntimeError(f"{name}: invalid codec {item.get('codec')}")
        allowed_codecs = {
            "enum": {"enum-map"}, "struct": {"fields"},
            "fixed-array": {"fixed-array", "counted-array"},
            "ref": {"ref-alias"},
            "host-ref": {"binding-ref", "tagged-reference"},
            "span": {"span-registry", "profile-sequence"},
        }
        if item["codec"] not in allowed_codecs[item["kind"]]:
            raise RuntimeError(f"{name}: codec does not match kind")
        if item["codec"] == "tagged-reference" and item.get("tags") != [
            {"name": "none", "value": 0},
            {"name": "kinematics", "value": 1},
            {"name": "pose", "value": 2},
        ]:
            raise RuntimeError(
                f"{name}: tagged reference requires none/kinematics/pose tags"
            )
        if item["codec"] == "profile-sequence" and (
            item.get("task_period") != "required" or
            item.get("time_rounding") != "ceil"
        ):
            raise RuntimeError(f"{name}: profile sequence requires ceiling task-period conversion")
        if item.get("size", 0) <= 0 or item.get("alignment", 0) <= 0:
            raise RuntimeError(f"{name}: invalid layout")
    if abi != {"enum_base": "DINT", "handle_type": "ULINT", "handle_null": 0}:
        raise RuntimeError("unexpected ST binding ABI")
    return abi, types


def verify_pin_coverage(types: list[dict]) -> None:
    if not PIN_CATALOG.exists():
        raise RuntimeError(f"missing generated pin catalog: {PIN_CATALOG}")
    pin_types = set(re.findall(r"^\s+st_type: '([^']+)'$",
                               PIN_CATALOG.read_text(encoding="utf-8"), re.M))
    pin_types -= set(BUILTINS)
    pin_types.discard("UNRESOLVED")
    nested_types: set[str] = set()

    def collect_type(spec: object) -> None:
        if isinstance(spec, str):
            if spec not in BUILTINS:
                nested_types.add(spec)
            return
        if not isinstance(spec, dict):
            return
        if "array" in spec:
            collect_type(spec["array"])
        if "element" in spec:
            collect_type(spec["element"])
        for field in spec.get("fields", []):
            collect_type(field.get("type"))

    for item in types:
        collect_type(item.get("element"))
        for field in item.get("fields", []):
            collect_type(field.get("type"))
    authority_types = {item["name"] for item in types}
    required_types = pin_types | nested_types
    if required_types != authority_types:
        missing = sorted(required_types - authority_types)
        extra = sorted(authority_types - required_types)
        raise RuntimeError(
            f"type authority/pin catalog mismatch: missing={missing}, extra={extra}"
        )


class Model:
    def __init__(self) -> None:
        self.descs: list[Desc] = []
        self.by_name: dict[str, Desc] = {}
        self.public: dict[str, Desc] = {}

    def resolve(self, name: str) -> tuple[int, int, int]:
        if name in BUILTINS:
            return BUILTINS[name]
        desc = self.by_name.get(name)
        if desc is None:
            raise RuntimeError(f"type dependency {name} must precede its use")
        return desc.type_id, desc.size, desc.alignment

    def append(self, desc: Desc, public: bool = False) -> Desc:
        if desc.name in self.by_name or desc.name in BUILTINS:
            raise RuntimeError(f"duplicate installed type {desc.name}")
        self.descs.append(desc)
        self.by_name[desc.name] = desc
        if public:
            self.public[desc.name] = desc
        return desc

    def next_id(self) -> int:
        return 0x10000 + len(self.descs)

    def add_array(self, name: str, element_name: str, count: int,
                  lower: int = 0, public: bool = False) -> Desc:
        element, size, alignment = self.resolve(element_name)
        desc = Desc(name, "array", self.next_id(), size * count, alignment,
                    element=element,
                    bounds=[(lower, lower + count - 1, count, size)])
        return self.append(desc, public)

    def add_struct(self, name: str, specs: list[dict], public: bool = False) -> Desc:
        offset = 0
        alignment = 1
        fields: list[tuple[str, int, int]] = []
        for spec in specs:
            type_id, size, field_alignment = self.resolve(spec["type"])
            field_alignment = min(field_alignment, 8)
            offset = align_up(offset, field_alignment)
            fields.append((spec["name"], type_id, offset))
            offset += size
            alignment = max(alignment, field_alignment)
        desc = Desc(name, "struct", self.next_id(), align_up(offset, alignment),
                    alignment, fields=fields)
        return self.append(desc, public)

    def add_public(self, item: dict) -> None:
        name = item["name"]
        kind = item["kind"]
        if kind == "enum":
            desc = Desc(name, "enum", self.next_id(), 4, 4,
                        enum_base=BUILTINS["DINT"][0], enum_items=item["values"])
            self.append(desc, True)
        elif kind in {"host-ref", "span"}:
            self.append(Desc(name, "ref", self.next_id(), 8, 8), True)
        elif kind == "ref":
            target, _, _ = self.resolve(item["target"])
            self.append(Desc(name, "ref", self.next_id(), 8, 8, target=target), True)
        elif kind == "struct":
            specs = self.materialize_fields(name, item["fields"])
            self.add_struct(name, specs, True)
        elif kind == "fixed-array":
            element = item["element"]
            element_name = self.materialize_element(name, element)
            if "count" in item:
                array_name = f"__ST_BINDING_{name}_VALUE"
                self.add_array(array_name, element_name, item["capacity"])
                self.add_struct(name, [
                    {"name": "value", "type": array_name},
                    {"name": item["count"]["name"], "type": item["count"]["type"]},
                ], True)
            else:
                self.add_array(name, element_name, item["capacity"], public=True)
        else:
            raise RuntimeError(f"{name}: unsupported kind {kind}")
        desc = self.public[name]
        if (desc.size, desc.alignment) != (item["size"], item["alignment"]):
            raise RuntimeError(
                f"{name}: expected {item['size']}/{item['alignment']}, "
                f"computed {desc.size}/{desc.alignment}"
            )

    def materialize_fields(self, owner: str, fields: list[dict]) -> list[dict]:
        result: list[dict] = []
        for item in fields:
            value = item["type"]
            if isinstance(value, dict):
                helper = f"__ST_BINDING_{owner}_{item['name'].upper()}"
                self.add_array(helper, value["array"], value["count"], value.get("lower", 0))
                value = helper
            result.append({"name": item["name"], "type": value})
        return result

    def materialize_element(self, owner: str, element: str | dict) -> str:
        if isinstance(element, str):
            return element
        helper = f"__ST_BINDING_{owner}_ELEMENT"
        specs = self.materialize_fields(helper, element["fields"])
        desc = self.add_struct(helper, specs)
        if (desc.size, desc.alignment) != (element["size"], element["alignment"]):
            raise RuntimeError(
                f"{owner} element: expected {element['size']}/{element['alignment']}, "
                f"computed {desc.size}/{desc.alignment}"
            )
        return helper


def build_model(types: list[dict]) -> Model:
    model = Model()
    for item in types:
        model.add_public(item)
    if len(model.public) != 49:
        raise RuntimeError("public type model is incomplete")
    return model


def fnv_byte(value: int, state: int) -> int:
    return ((state ^ value) * 1099511628211) & 0xFFFFFFFFFFFFFFFF


def fnv_u64(value: int, state: int) -> int:
    value &= 0xFFFFFFFFFFFFFFFF
    for shift in range(0, 64, 8):
        state = fnv_byte((value >> shift) & 0xFF, state)
    return state


def fnv_text(value: str, state: int) -> int:
    for byte in value.encode("utf-8"):
        state = fnv_byte(byte, state)
    return fnv_byte(0, state)


def shape_hash(desc: Desc) -> int:
    state = fnv_text(desc.name, 1469598103934665603)
    state = fnv_u64(KIND_CODE[desc.kind], state)
    state = fnv_u64(desc.size, state)
    state = fnv_u64(desc.alignment, state)
    if desc.kind == "enum":
        state = fnv_u64(desc.enum_base, state)
        state = fnv_u64(len(desc.enum_items), state)
        for item in desc.enum_items:
            state = fnv_text(item["name"], state)
            state = fnv_u64(item["iec"], state)
    elif desc.kind == "array":
        state = fnv_u64(desc.element, state)
        state = fnv_u64(len(desc.bounds), state)
        for values in desc.bounds:
            for value in values:
                state = fnv_u64(value, state)
    elif desc.kind == "struct":
        state = fnv_u64(len(desc.fields), state)
        for name, type_id, offset in desc.fields:
            state = fnv_text(name, state)
            state = fnv_u64(type_id, state)
            state = fnv_u64(offset, state)
    elif desc.kind == "ref":
        state = fnv_u64(desc.target, state)
    return state


def type_cpp(model: Model, name: str) -> str:
    if name in BUILTIN_CPP:
        return BUILTIN_CPP[name]
    return f"binding_type::{identifier(name)}"


def field_layout(model: Model, fields: list[dict]) -> list[dict]:
    offset = 0
    result: list[dict] = []
    for item in fields:
        value = item["type"]
        if isinstance(value, dict):
            _, size, alignment = model.resolve(value["array"])
            size *= value["count"]
            display = f"ARRAY[{value.get('lower', 0)}..{value.get('lower', 0) + value['count'] - 1}] OF {value['array']}"
        else:
            _, size, alignment = model.resolve(value)
            display = value
        alignment = min(alignment, 8)
        offset = align_up(offset, alignment)
        result.append({**item, "st_type": display, "offset": offset,
                       "size": size, "alignment": alignment})
        offset += size
    return result


def metadata_fields(model: Model, item: dict) -> list[dict]:
    if item["kind"] == "struct":
        fields = field_layout(model, item["fields"])
        return [{**field, "element": False} for field in fields]
    if item["kind"] in {"fixed-array", "span"} and isinstance(item["element"], dict):
        fields = field_layout(model, item["element"]["fields"])
        result = [{**field, "element": True} for field in fields]
        if "count" in item:
            result.append({"name": item["count"]["name"], "st_type": item["count"]["type"],
                           "native": item["count"]["native"], "offset": item["size"] - 8,
                           "size": 8, "alignment": 8, "element": False})
        return result
    if item["kind"] == "fixed-array" and "count" in item:
        return [{"name": item["count"]["name"], "st_type": item["count"]["type"],
                 "native": item["count"]["native"], "offset": item["size"] - 8,
                 "size": 8, "alignment": 8, "element": False}]
    return []


def render(types: list[dict], model: Model) -> str:
    constants = [
        f"inline constexpr TypeId {identifier(name)} = {desc.type_id}U;"
        for name, desc in model.public.items()
    ]
    install_rows: list[str] = []
    for desc in model.descs:
        expected = f"{desc.type_id}U"
        if desc.kind == "enum":
            items = ", ".join(
                "EnumItem{%s, IntegerValue::signed_value(%d)}" %
                (cpp_quote(item["name"]), item["iec"])
                for item in desc.enum_items
            )
            call = f"types.add_enum({cpp_quote(desc.name)}, builtin::dint, {{{items}}}, id)"
        elif desc.kind == "array":
            lower, upper, _, _ = desc.bounds[0]
            call = (f"types.add_array({cpp_quote(desc.name)}, {desc.element}U, "
                    f"{{ArrayBound{{{lower}, {upper}}}}}, id)")
        elif desc.kind == "struct":
            fields = ", ".join(
                f"StructFieldSpec{{{cpp_quote(name)}, {type_id}U}}"
                for name, type_id, _ in desc.fields
            )
            call = f"types.add_struct({cpp_quote(desc.name)}, {{{fields}}}, id)"
        else:
            if desc.target == 0:
                call = f"types.add_opaque_ref({cpp_quote(desc.name)}, id)"
            else:
                call = f"types.add_ref({cpp_quote(desc.name)}, {desc.target}U, id)"
        install_rows.extend([
            f"    error = {call};",
            f"    if(error != TypeError::ok || id != {expected})",
            "        return error == TypeError::ok ? TypeError::duplicate_type : error;",
        ])

    enum_rows: list[str] = []
    field_rows: list[str] = []
    type_rows: list[str] = []
    first_enum = 0
    first_field = 0
    kind_cpp = {
        "enum": "enum_", "struct": "struct_", "fixed-array": "fixed_array",
        "ref": "ref", "host-ref": "host_ref", "span": "span",
    }
    codec_cpp = {value: value.replace("-", "_") for value in CODECS}
    for item in types:
        desc = model.public[item["name"]]
        values = item.get("values", [])
        fields = metadata_fields(model, item)
        for value in values:
            enum_rows.append(
                f"    {{{desc.type_id}U, {cpp_quote(value['name'])}, {value['iec']}, {value['native']}}},"
            )
        for value in fields:
            field_rows.append(
                "    {%dU, %s, %s, %s, %dU, %dU, %dU, %s}," %
                (desc.type_id, cpp_quote(value["name"]), cpp_quote(value["st_type"]),
                 cpp_quote(value.get("native", "")), value["offset"], value["size"],
                 value["alignment"], "true" if value["element"] else "false")
            )
        element = item.get("element")
        element_size = element.get("size", 0) if isinstance(element, dict) else 0
        element_alignment = element.get("alignment", 0) if isinstance(element, dict) else 0
        if isinstance(element, str):
            _, element_size, element_alignment = model.resolve(element)
        required = "true" if item.get("task_period") == "required" else "false"
        rounding = "ceil" if item.get("time_rounding") == "ceil" else "none"
        type_rows.append(
            f"    {{{desc.type_id}U, {cpp_quote(item['name'])}, "
            f"StBindingTypeKind::{kind_cpp[item['kind']]}, "
            f"StBindingCodecKind::{codec_cpp[item['codec']]}, "
            f"{item['size']}U, {item['alignment']}U, {item.get('capacity', 0)}U, "
            f"{item.get('min_count', 0)}U, {item.get('max_count', 0)}U, "
            f"{element_size}U, {element_alignment}U, {first_enum}U, "
            f"{len(values)}U, {first_field}U, {required}, "
            f"StBindingTimeRounding::{rounding}, {len(fields)}U}},"
        )
        first_enum += len(values)
        first_field += len(fields)

    shape_rows = [
        f"    {{{desc.type_id}U, 0x{shape_hash(desc):016x}ULL}},"
        for desc in model.descs
    ]
    lines = [
        "#pragma once", "",
        "// Generated by tools/generate_st_binding_types.py; do not edit.",
        "// Source: doc/compliance/st-binding-types.yml", "",
        "#include <array>", "#include <cstdint>", "#include <limits>",
        "#include <string_view>", "",
        "namespace plcopen::core::st", "{", "",
        "namespace binding_type", "{", *constants,
        f"inline constexpr std::size_t public_count = {len(types)}U;",
        f"inline constexpr std::size_t count = {len(model.descs)}U;",
        "} // namespace binding_type", "",
        "namespace generated", "{", "",
        "enum class StBindingTypeKind : std::uint8_t",
        "{ enum_, struct_, fixed_array, ref, host_ref, span };", "",
        "enum class StBindingCodecKind : std::uint8_t",
        "{ enum_map, fields, fixed_array, counted_array, binding_ref, ref_alias,",
        "  span_registry, profile_sequence, tagged_reference };", "",
        "enum class StBindingTimeRounding : std::uint8_t { none, ceil };", "",
        "struct StBindingEnumMapping",
        "{ TypeId type; std::string_view name; std::int64_t iec_value; std::int64_t native_value; };",
        "struct StBindingFieldMetadata",
        "{",
        "    TypeId owner; std::string_view name; std::string_view st_type;",
        "    std::string_view native_member; std::uint32_t offset; std::uint32_t size;",
        "    std::uint32_t alignment; bool element_field;",
        "};",
        "struct StBindingTypeMetadata",
        "{",
        "    TypeId type; std::string_view name; StBindingTypeKind kind; StBindingCodecKind codec;",
        "    std::uint32_t size; std::uint32_t alignment; std::uint32_t capacity;",
        "    std::uint32_t min_count; std::uint32_t max_count; std::uint32_t element_size;",
        "    std::uint32_t element_alignment; std::uint16_t first_enum; std::uint16_t enum_count;",
        "    std::uint16_t first_field; bool requires_task_period;",
        "    StBindingTimeRounding time_rounding; std::uint16_t field_count;",
        "};", "",
        f"inline constexpr std::array<StBindingEnumMapping, {len(enum_rows)}> kStBindingEnumMappings = {{{{",
        *enum_rows, "}};", "",
        f"inline constexpr std::array<StBindingFieldMetadata, {len(field_rows)}> kStBindingFields = {{{{",
        *field_rows, "}};", "",
        f"inline constexpr std::array<StBindingTypeMetadata, {len(types)}> kStBindingTypes = {{{{",
        *type_rows, "}};", "",
        "inline constexpr std::uint64_t st_binding_null_handle = 0U;",
        "constexpr bool st_binding_handle_valid(std::uint64_t handle) noexcept { return handle != 0U; }", "",
        "constexpr const StBindingTypeMetadata *st_binding_type_metadata(TypeId type) noexcept",
        "{",
        "    for(const auto &metadata : kStBindingTypes) if(metadata.type == type) return &metadata;",
        "    return nullptr;",
        "}", "",
        "constexpr bool st_binding_enum_to_native(TypeId type, std::int64_t iec,",
        "                                         std::int64_t &native) noexcept",
        "{",
        "    for(const auto &mapping : kStBindingEnumMappings)",
        "        if(mapping.type == type && mapping.iec_value == iec) { native = mapping.native_value; return true; }",
        "    return false;",
        "}", "",
        "constexpr bool st_binding_enum_from_native(TypeId type, std::int64_t native,",
        "                                           std::int64_t &iec) noexcept",
        "{",
        "    for(const auto &mapping : kStBindingEnumMappings)",
        "        if(mapping.type == type && mapping.native_value == native) { iec = mapping.iec_value; return true; }",
        "    return false;",
        "}", "",
        "constexpr bool st_binding_time_ns_to_cycles(std::int64_t time_ns,",
        "                                             std::int64_t task_period_ns,",
        "                                             std::int64_t &cycles) noexcept",
        "{",
        "    if(time_ns < 0 || task_period_ns <= 0) return false;",
        "    const std::int64_t quotient = time_ns / task_period_ns;",
        "    const std::int64_t remainder = time_ns % task_period_ns;",
        "    if(remainder != 0 && quotient == std::numeric_limits<std::int64_t>::max()) return false;",
        "    cycles = quotient + (remainder != 0 ? 1 : 0);",
        "    return true;",
        "}", "",
        "struct StBindingInstalledShape { TypeId type; std::uint64_t hash; };",
        f"inline constexpr std::array<StBindingInstalledShape, {len(shape_rows)}> kStBindingInstalledShapes = {{{{",
        *shape_rows, "}};", "",
        "constexpr std::uint64_t st_binding_hash_byte(std::uint64_t state, std::uint8_t value) noexcept",
        "{ return (state ^ value) * 1099511628211ULL; }",
        "constexpr std::uint64_t st_binding_hash_u64(std::uint64_t state, std::uint64_t value) noexcept",
        "{ for(unsigned shift = 0; shift < 64; shift += 8) state = st_binding_hash_byte(state, static_cast<std::uint8_t>(value >> shift)); return state; }",
        "inline std::uint64_t st_binding_hash_text(std::uint64_t state, std::string_view value) noexcept",
        "{ for(char ch : value) state = st_binding_hash_byte(state, static_cast<std::uint8_t>(ch)); return st_binding_hash_byte(state, 0); }",
        "inline std::uint64_t st_binding_shape_hash(const TypeDesc &desc) noexcept",
        "{",
        "    std::uint64_t state = st_binding_hash_text(1469598103934665603ULL, desc.name);",
        "    state = st_binding_hash_u64(state, static_cast<std::uint8_t>(desc.kind));",
        "    state = st_binding_hash_u64(state, desc.size); state = st_binding_hash_u64(state, desc.alignment);",
        "    if(desc.kind == TypeKind::enum_) {",
        "        state = st_binding_hash_u64(state, desc.enum_base); state = st_binding_hash_u64(state, desc.enum_items.size());",
        "        for(const auto &item : desc.enum_items) { state = st_binding_hash_text(state, item.name); state = st_binding_hash_u64(state, static_cast<std::uint64_t>(item.value.as_signed())); }",
        "    } else if(desc.kind == TypeKind::array) {",
        "        state = st_binding_hash_u64(state, desc.array.element); state = st_binding_hash_u64(state, desc.array.dimensions.size());",
        "        for(const auto &dim : desc.array.dimensions) { state = st_binding_hash_u64(state, static_cast<std::uint64_t>(dim.lower)); state = st_binding_hash_u64(state, static_cast<std::uint64_t>(dim.upper)); state = st_binding_hash_u64(state, dim.extent); state = st_binding_hash_u64(state, dim.stride); }",
        "    } else if(desc.kind == TypeKind::struct_) {",
        "        state = st_binding_hash_u64(state, desc.structure.fields.size());",
        "        for(const auto &item : desc.structure.fields) { state = st_binding_hash_text(state, item.name); state = st_binding_hash_u64(state, item.type); state = st_binding_hash_u64(state, item.offset); }",
        "    } else if(desc.kind == TypeKind::ref) state = st_binding_hash_u64(state, desc.reference.target);",
        "    return state;",
        "}", "",
        "inline bool st_binding_installed_types_match(const TypeTable &types) noexcept",
        "{",
        "    if(types.next_type_id() != first_load_type_id + binding_type::count) return false;",
        "    for(const auto &shape : kStBindingInstalledShapes) { const TypeDesc *desc = types.get(shape.type); if(desc == nullptr || st_binding_shape_hash(*desc) != shape.hash) return false; }",
        "    return true;",
        "}", "",
        "} // namespace generated", "",
        "inline TypeError install_binding_types(TypeTable &types)",
        "{",
        "    if(types.next_type_id() != first_load_type_id)",
        "        return generated::st_binding_installed_types_match(types) ? TypeError::ok : TypeError::duplicate_type;",
        "    TypeId id = invalid_type_id; TypeError error = TypeError::ok;",
        *install_rows,
        "    return generated::st_binding_installed_types_match(types) ? TypeError::ok : TypeError::duplicate_type;",
        "}", "",
        f"static_assert(binding_type::public_count == {len(types)}U);",
        f"static_assert(binding_type::count == {len(model.descs)}U);",
        "", "} // namespace plcopen::core::st", "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    _, types = read_authority()
    verify_pin_coverage(types)
    model = build_model(types)
    rendered = render(types, model)
    if args.update:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(rendered, encoding="utf-8", newline="\n")
    elif not OUTPUT.exists():
        print(f"missing generated ST binding types: {OUTPUT}", file=sys.stderr)
        return 1
    elif OUTPUT.read_text(encoding="utf-8") != rendered:
        print("generated ST binding types are stale; run with --update", file=sys.stderr)
        return 1
    print(f"ST binding types: public={len(types)}, installed={len(model.descs)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build the ST-L2c pin catalog from the checked-in authorities.

The extractor is intentionally conservative.  A normative pin without an
unambiguous public native member or type is emitted as UNRESOLVED and makes
strict verification fail; it is never guessed into a registered dispatcher.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import re
import sys
from collections import defaultdict


ROOT = pathlib.Path(__file__).resolve().parents[1]
FB_CATALOG = ROOT / "doc/compliance/st-binding-fb-catalog.yml"
PIN_CATALOG = ROOT / "doc/compliance/generated/st-binding-fb-pins.yml"
GAP_CATALOG = ROOT / "doc/compliance/generated/st-binding-native-gaps.yml"
CPP_CATALOG = ROOT / "core/st/generated/st_binding_catalog.h"
NATIVE_CATALOG = ROOT / "core/st/generated/st_binding_native.h"
PIN_DESC_CATALOG = ROOT / "core/st/generated/st_binding_pins.h"
ADAPTER_OVERLAY = ROOT / "doc/compliance/st-binding-adapters.yml"
TYPE_AUTHORITY = ROOT / "doc/compliance/st-binding-types.yml"

SET_STABLE_CODES = {
    "iec_basic": 0x0001,
    "plcopen_part1_part2": 0x0002,
    "plcopen_part4": 0x0003,
    "plcopen_part5": 0x0004,
}

@dataclasses.dataclass
class Field:
    native_type: str
    name: str


@dataclasses.dataclass
class Record:
    name: str
    kind: str
    bases: list[str]
    fields: list[Field]
    body: str


@dataclasses.dataclass
class Pin:
    name: str
    direction: str
    st_type: str = ""
    native_member: str = ""
    native_type: str = ""
    type_role: str = ""
    resolution: str = "mapped"
    adapter: str = "unresolved"


@dataclasses.dataclass
class Fb:
    set_name: str
    name: str
    native: str
    source: str
    pins: list[Pin]
    resolution: str = "mapped"
    lifecycle: str = "call"
    task_period: str = "none"


@dataclasses.dataclass
class AdapterPin:
    name: str
    st_type: str
    type_role: str
    native_member: str
    native_type: str
    adapter: str


@dataclasses.dataclass
class AdapterFb:
    name: str
    native: str = ""
    lifecycle: str = ""
    task_period: str = ""
    pins: dict[str, AdapterPin] = dataclasses.field(default_factory=dict)


def normalize(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def pin_to_snake(value: str) -> str:
    value = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value)
    value = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", value)
    return value.lower()


BUILTIN_PIN_TYPES = {
    "BOOL": ("Type::bool_", "builtin::bool_"),
    "SINT": ("Type::sint", "builtin::sint"),
    "INT": ("Type::int_", "builtin::int_"),
    "DINT": ("Type::dint", "builtin::dint"),
    "LINT": ("Type::lint", "builtin::lint"),
    "USINT": ("Type::usint", "builtin::usint"),
    "UINT": ("Type::uint_", "builtin::uint_"),
    "UDINT": ("Type::udint", "builtin::udint"),
    "ULINT": ("Type::ulint", "builtin::ulint"),
    "REAL": ("Type::real", "builtin::real"),
    "LREAL": ("Type::lreal", "builtin::lreal"),
    "TIME": ("Type::time", "builtin::time"),
    "BYTE": ("Type::byte_", "builtin::byte_"),
    "WORD": ("Type::word", "builtin::word"),
    "DWORD": ("Type::dword", "builtin::dword"),
    "LWORD": ("Type::lword", "builtin::lword"),
    "DATE": ("Type::date", "builtin::date"),
    "TOD": ("Type::tod", "builtin::tod"),
    "DT": ("Type::dt", "builtin::dt"),
}

FLOAT_PIN_TYPES = {"REAL", "LREAL"}
UNSIGNED_PIN_TYPES = {
    "USINT", "UINT", "UDINT", "ULINT", "BYTE", "WORD", "DWORD", "LWORD"
}

OBJECT_CODEC_TYPES = {
    "MC_IDENT_IN_GROUP",
    "MC_REFERENCE_SIGNAL_REF",
    "MC_TOOL_DATA",
    "MC_GROUP_POSITION",
    "MC_PAYLOAD_DATA",
    "MC_JOG_BOOLEAN_ARRAY",
    "MC_GROUP_S_W_LIMITS",
    "MC_RIGID_BODY_DYNAMIC",
    "MC_TU_C_NUMERATOR",
    "MC_TU_C_DENOMINATOR",
    "MC_DH_PARAMETER_ARRAY",
    "MC_JOINT_INFO_ARRAY",
    "MC_AXIS_VELOCITY_ARRAY",
    "MC_AXIS_ACCELERATION_ARRAY",
    "MC_AXIS_DECELERATION_ARRAY",
    "MC_AXIS_JERK_ARRAY",
}

SEQUENCE_CODEC_TYPES = {
    "MC_PATH_TABLE",
    "MC_PATH_DESCRIPTION",
    "MC_CAM_SWITCH_TABLE_VIEW",
    "MC_CAM_SWITCH_OUTPUTS_VIEW",
    "MC_CAM_TRACK_OPTIONS_VIEW",
    "MC_CAM_TABLE_VIEW",
    "MC_TIME_POSITION",
    "MC_TIME_VELOCITY",
    "MC_TIME_ACCELERATION",
}

TAGGED_REFERENCE_CODEC_TYPES = {"MC_KIN_TRANSFORM_REF"}


def read_binding_type_authority() -> dict[str, dict[str, object]]:
    lines = TYPE_AUTHORITY.read_text(encoding="utf-8").splitlines()
    if "schema: plcopen-st-binding-types-v1" not in lines:
        raise RuntimeError("unexpected ST binding type authority schema")
    result: dict[str, dict[str, object]] = {}
    for line in lines:
        stripped = line.strip()
        if not stripped.startswith("- {"):
            continue
        item = json.loads(stripped[2:])
        name = item.get("name")
        if not isinstance(name, str) or name in result:
            raise RuntimeError(f"invalid or duplicate binding type {name}")
        result[name] = item
    if not result:
        raise RuntimeError("empty ST binding type authority")
    return result


def pin_cpp_type(st_type: str,
                 binding_types: dict[str, dict[str, object]]) -> tuple[str, str]:
    if st_type == "UNRESOLVED":
        return "Type::bool_", "invalid_type_id"
    builtin = BUILTIN_PIN_TYPES.get(st_type)
    if builtin is not None:
        return builtin
    metadata = binding_types.get(st_type)
    if metadata is None:
        raise RuntimeError(f"pin type absent from type authority: {st_type}")
    kind = metadata.get("kind")
    if st_type == "AXIS_REF":
        carrier = "Type::axis_ref"
    elif st_type == "GROUP_REF":
        carrier = "Type::group_ref"
    elif kind == "enum":
        carrier = "Type::dint"
    elif kind in {"host-ref", "ref", "span"}:
        # Only the two builtin binding references use the dedicated language
        # carriers above. Named ref aliases remain opaque 64-bit registry
        # handles; their target kind is recovered from the nominal TypeId.
        carrier = "Type::ulint"
    elif kind in {"struct", "fixed-array"}:
        # Aggregate identity and layout are carried by TypeId. Type retains
        # the established scalar carrier used by Expected/ExprInfo.
        carrier = "Type::bool_"
    else:
        raise RuntimeError(f"unsupported pin type kind {st_type}:{kind}")
    return carrier, f"binding_type::{pin_to_snake(st_type)}"


def scalar_binding_kind(st_type: str,
                        binding_types: dict[str, dict[str, object]]) -> str:
    if st_type in BUILTIN_PIN_TYPES:
        return "builtin"
    metadata = binding_types.get(st_type)
    if metadata is None:
        return ""
    kind = str(metadata.get("kind", ""))
    return kind if kind in {"enum", "host-ref", "ref"} else ""


def parse_native_enum_values() -> dict[str, list[int]]:
    text = strip_comments("\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((ROOT / "core").rglob("*.h"))
    ))
    result: dict[str, list[int]] = {}
    header_re = re.compile(
        r"\benum\s+class\s+([A-Za-z_]\w*)\s*(?::\s*[^\{]+)?\{"
    )
    for match in header_re.finditer(text):
        name = match.group(1)
        close = matching_brace(text, match.end() - 1)
        body = text[match.end():close]
        value = -1
        values: list[int] = []
        supported = True
        for raw in body.split(","):
            item = raw.strip()
            if not item:
                continue
            if "=" in item:
                _, expression = item.split("=", 1)
                expression = expression.strip()
                if not re.fullmatch(
                    r"[-+]?(?:0[xX][0-9A-Fa-f]+|[0-9]+)", expression
                ):
                    supported = False
                    break
                value = int(expression, 0)
            else:
                value += 1
            values.append(value)
        if supported and values:
            result[name] = values
    return result


def quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def cpp_quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse_adapter_overlay() -> dict[str, AdapterFb]:
    if not ADAPTER_OVERLAY.exists():
        raise RuntimeError(f"missing adapter overlay: {ADAPTER_OVERLAY}")
    lines = ADAPTER_OVERLAY.read_text(encoding="utf-8").splitlines()
    if "schema: plcopen-st-binding-adapters-v1" not in lines:
        raise RuntimeError("unexpected ST binding adapter overlay schema")
    summary_re = re.compile(r"^summary: \{fbs: ([0-9]+), pins: ([0-9]+)\}$")
    name_re = re.compile(r"^  - name: '([^']+)'$")
    native_re = re.compile(r"^    native: '([^']+)'$")
    lifecycle_re = re.compile(r"^    lifecycle: (cycle|call)$")
    task_period_re = re.compile(r"^    task_period: (none|set_cycle_time|required)$")
    pin_re = re.compile(
        r"^      - \{name: '([^']+)', st_type: '([^']+)', "
        r"type_role: (value|nominal|ref|object), "
        r"native_member: '([^']+)', native_type: '([^']+)', "
        r"adapter: (direct|enum_cast|binding_ref|sequence|constant|tagged_reference)\}$"
    )
    result: dict[str, AdapterFb] = {}
    current: AdapterFb | None = None
    expected_fbs = -1
    expected_pins = -1
    for line in lines:
        match = summary_re.match(line)
        if match:
            expected_fbs = int(match.group(1))
            expected_pins = int(match.group(2))
            continue
        match = name_re.match(line)
        if match:
            name = match.group(1)
            if name in result:
                raise RuntimeError(f"duplicate adapter FB {name}")
            current = AdapterFb(name)
            result[name] = current
            continue
        if current is None:
            continue
        match = native_re.match(line)
        if match:
            current.native = match.group(1)
            continue
        match = lifecycle_re.match(line)
        if match:
            current.lifecycle = match.group(1)
            continue
        match = task_period_re.match(line)
        if match:
            current.task_period = match.group(1)
            continue
        match = pin_re.match(line)
        if match:
            name = match.group(1)
            if name in current.pins:
                raise RuntimeError(f"duplicate adapter pin {current.name}.{name}")
            current.pins[name] = AdapterPin(
                name, match.group(2), match.group(3), match.group(4),
                match.group(5), match.group(6)
            )
    actual_pins = sum(len(fb.pins) for fb in result.values())
    if len(result) != expected_fbs or actual_pins != expected_pins:
        raise RuntimeError(
            "adapter overlay summary mismatch: "
            f"expected {expected_fbs} FB/{expected_pins} pins, "
            f"got {len(result)} FB/{actual_pins} pins"
        )
    return result


def read_catalog() -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    pattern = re.compile(
        r"^  - \{set: ([a-z0-9_]+), name: ([A-Za-z0-9_]+), "
        r"native: fb::([A-Za-z0-9_]+)\}$"
    )
    for line in FB_CATALOG.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            rows.append((match.group(1), match.group(2), match.group(3)))
    if len(rows) != 134:
        raise RuntimeError(f"expected 134 FB catalog rows, got {len(rows)}")
    return rows


def parse_restricted_pin_yaml(path: pathlib.Path) -> dict[str, list[Pin]]:
    result: dict[str, list[Pin]] = defaultdict(list)
    current = ""
    name_re = re.compile(r"^  - name: ([A-Za-z0-9_]+)$")
    pin_re = re.compile(
        r"^      - \{direction: (input|output|in_out), name: ([A-Za-z0-9_]+), "
        r"st_type: ([A-Za-z0-9_]+), native_member: ([A-Za-z0-9_.]+)\}$"
    )
    for line in path.read_text(encoding="utf-8").splitlines():
        match = name_re.match(line)
        if match:
            current = match.group(1)
            continue
        match = pin_re.match(line)
        if match and current:
            result[current].append(Pin(match.group(2), match.group(1),
                                       match.group(3), match.group(4)))
    return dict(result)


def parse_motion_yaml(path: pathlib.Path, variants: bool,
                      expected_schema: str) -> dict[str, list[Pin]]:
    result: dict[str, list[Pin]] = defaultdict(list)
    current_names: list[str] = []
    current_variant = 0
    seen: set[str] = set()
    name_re = re.compile(r"^    name: '([^']+)'$")
    variants_re = re.compile(r"^    variants:\s*(.+)$")
    pin_re = re.compile(
        r"^      - \{direction: (input|output|in_out), class: [BE], "
        r"name: ([A-Za-z][A-Za-z0-9_]*)(?:, st_type: ([A-Za-z0-9_]+))?"
        r"(?:, binding: metadata_only)?"
        r"(?:, support: (?:Yes|No), evidence: '[^']*')?\}$"
    )
    if not path.exists():
        raise RuntimeError(f"missing normative pin authority: {path}")
    lines = path.read_text(encoding="utf-8").splitlines()
    if f"schema: {expected_schema}" not in lines:
        raise RuntimeError(f"unexpected pin authority schema in {path}")
    for line in lines:
        match = name_re.match(line)
        if match:
            current_names = [match.group(1)]
            current_variant = 0
            seen.clear()
            continue
        match = variants_re.match(line)
        if variants and match and current_names:
            current_names = [item.strip() for item in match.group(1).split(",")]
            current_variant = 0
            seen.clear()
            continue
        match = pin_re.match(line)
        if not match or not current_names:
            continue
        key = normalize(match.group(2))
        if key in seen and key == "axis" and current_variant + 1 < len(current_names):
            current_variant += 1
            seen.clear()
        if key in seen:
            raise RuntimeError(
                f"duplicate pin {match.group(2)} in {current_names[current_variant]}"
            )
        seen.add(key)
        result[current_names[current_variant]].append(
            Pin(match.group(2), match.group(1), match.group(3) or "")
        )
    return dict(result)


def matching_brace(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise RuntimeError("unmatched C++ record brace")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def public_fields(kind: str, body: str) -> list[Field]:
    access = "public" if kind == "struct" else "private"
    fields: list[Field] = []
    buffer: list[str] = []
    index = 0
    while index < len(body):
        if body[index] == "{":
            prefix = "".join(buffer)
            close = matching_brace(body, index)
            if "(" in prefix:
                buffer.clear()
                index = close + 1
                continue
            buffer.append(body[index:close + 1])
            index = close + 1
            continue
        if body[index] == ":":
            token = "".join(buffer).strip()
            if token in {"public", "private", "protected"}:
                access = token
                buffer.clear()
                index += 1
                continue
        if body[index] == ";":
            statement = " ".join("".join(buffer).split())
            buffer.clear()
            index += 1
            if access != "public" or not statement or "(" in statement:
                continue
            if statement.startswith(("using ", "typedef ", "friend ",
                                     "static ", "enum ", "class ", "struct ")):
                continue
            declaration = re.sub(r"\s*=.*$", "", statement).strip()
            declaration = re.sub(r"\s*\{.*\}$", "", declaration).strip()
            declaration = re.sub(r"\s*\[[^]]*\]$", "", declaration).strip()
            match = re.match(r"^(.+?[\s*&])([A-Za-z_]\w*)$", declaration)
            if match:
                fields.append(Field(match.group(1).strip(), match.group(2)))
            continue
        buffer.append(body[index])
        index += 1
    return fields


def parse_records() -> tuple[dict[str, Record], dict[str, str]]:
    chunks = []
    record_paths = [
        *sorted((ROOT / "core/fb").glob("*.h")),
        ROOT / "core/axis/group.h",
    ]
    for path in record_paths:
        chunks.append(path.read_text(encoding="utf-8"))
    text = strip_comments("\n".join(chunks))
    records: dict[str, Record] = {}
    header_re = re.compile(
        r"\b(class|struct)\s+([A-Za-z_]\w*)\s*([^;{]*)\{"
    )
    for match in header_re.finditer(text):
        kind, name, suffix = match.group(1), match.group(2), match.group(3)
        opening = match.end() - 1
        close = matching_brace(text, opening)
        body = text[opening + 1:close]
        bases: list[str] = []
        if ":" in suffix:
            for raw in suffix.split(":", 1)[1].split(","):
                tokens = raw.strip().split()
                if tokens and ("public" in tokens or kind == "struct"):
                    bases.append(tokens[-1].split("::")[-1])
        records[name] = Record(name, kind, bases, public_fields(kind, body), body)
    aliases = {
        match.group(1): match.group(2).split("::")[-1]
        for match in re.finditer(
            r"\busing\s+([A-Za-z_]\w*)\s*=\s*([A-Za-z_:]\w*)\s*;", text
        )
    }
    return records, aliases


def resolve_alias(name: str, aliases: dict[str, str]) -> str:
    seen: set[str] = set()
    while name in aliases and name not in seen:
        seen.add(name)
        name = aliases[name]
    return name


def hierarchy(name: str, records: dict[str, Record],
              aliases: dict[str, str]) -> list[Record]:
    name = resolve_alias(name, aliases)
    if name not in records:
        return []
    ordered: list[Record] = []
    seen: set[str] = set()

    def visit(record_name: str) -> None:
        record_name = resolve_alias(record_name, aliases)
        if record_name in seen or record_name not in records:
            return
        seen.add(record_name)
        record = records[record_name]
        for base in record.bases:
            visit(base)
        ordered.append(record)

    visit(name)
    return ordered


def native_fields(name: str, records: dict[str, Record],
                  aliases: dict[str, str]) -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    used: set[str] = set()

    def nested_record(native_type: str) -> str:
        if "*" in native_type or "&" in native_type or "<" in native_type:
            return ""
        tokens = re.sub(r"\b(?:const|volatile)\b", "", native_type).split()
        if len(tokens) != 1:
            return ""
        return resolve_alias(tokens[0].split("::")[-1], aliases)

    def append(record_name: str, prefix: str,
               ancestors: frozenset[str]) -> None:
        record_name = resolve_alias(record_name, aliases)
        if record_name in ancestors:
            return
        next_ancestors = ancestors | {record_name}
        for record in hierarchy(record_name, records, aliases):
            for field in record.fields:
                if field.name.startswith("_") or field.name.endswith("_"):
                    continue
                path = f"{prefix}.{field.name}" if prefix else field.name
                if path not in used:
                    result.append((path, field.native_type))
                    used.add(path)
                child = nested_record(field.native_type)
                if child in records:
                    append(child, path, next_ancestors)

    append(name, "", frozenset())
    return result


def bind_overlay_pins(fb_name: str, native: str, pins: list[Pin],
                      overlay: AdapterFb, records: dict[str, Record],
                      aliases: dict[str, str]) -> list[Pin]:
    fields = dict(native_fields(native, records, aliases))
    authority_names = {pin.name for pin in pins}
    unknown = set(overlay.pins) - authority_names
    if unknown:
        raise RuntimeError(
            f"{fb_name}: adapter pins absent from authority: "
            + ", ".join(sorted(unknown))
        )
    for pin in pins:
        adapter = overlay.pins.get(pin.name)
        if adapter is None:
            pin.native_member = "UNRESOLVED"
            pin.native_type = "UNRESOLVED"
            pin.adapter = "unresolved"
            pin.resolution = "unresolved_adapter"
            if not pin.st_type:
                pin.st_type = "UNRESOLVED"
                pin.type_role = "unresolved"
            elif pin.st_type.endswith("_REF"):
                pin.type_role = "ref"
            elif pin.st_type.startswith("MC_"):
                pin.type_role = "nominal"
            else:
                pin.type_role = "value"
            continue
        if pin.st_type and pin.st_type != adapter.st_type:
            raise RuntimeError(
                f"{fb_name}.{pin.name}: authority type {pin.st_type} "
                f"does not match adapter type {adapter.st_type}"
            )
        if adapter.adapter == "constant":
            if pin.direction != "output":
                raise RuntimeError(
                    f"{fb_name}.{pin.name}: constant adapter requires output"
                )
            expected_constant = {
                "false": ("BOOL", "value", "bool"),
                "0": ("UDINT", "value", "std::uint32_t"),
            }.get(adapter.native_member)
            actual_constant = (
                adapter.st_type, adapter.type_role, adapter.native_type
            )
            if expected_constant != actual_constant:
                raise RuntimeError(
                    f"{fb_name}.{pin.name}: invalid constant "
                    f"{adapter.native_member} for {actual_constant}"
                )
        elif adapter.adapter == "sequence":
            members = adapter.native_member.split(" + ")
            types = adapter.native_type.split(" + ")
            if len(members) != 2 or len(types) != 2:
                raise RuntimeError(
                    f"{fb_name}.{pin.name}: invalid sequence adapter"
                )
            actual_types = [fields.get(member) for member in members]
            if actual_types != types:
                raise RuntimeError(
                    f"{fb_name}.{pin.name}: sequence adapter drift; "
                    f"expected {types}, got {actual_types}"
                )
        else:
            actual_type = fields.get(adapter.native_member)
            if actual_type != adapter.native_type:
                raise RuntimeError(
                    f"{fb_name}.{pin.name}: adapter drift at "
                    f"{adapter.native_member}; expected {adapter.native_type}, "
                    f"got {actual_type or 'missing'}"
                )
        expected_kind = {
            "ref": "binding_ref",
            "nominal": "enum_cast",
        }.get(adapter.type_role, "direct")
        if adapter.adapter == "sequence":
            expected_kind = "sequence"
        elif adapter.adapter == "constant":
            expected_kind = "constant"
        elif adapter.adapter == "tagged_reference":
            expected_kind = "tagged_reference"
        if adapter.adapter != expected_kind:
            raise RuntimeError(
                f"{fb_name}.{pin.name}: adapter kind {adapter.adapter} "
                f"does not match type role {adapter.type_role}"
            )
        pin.st_type = adapter.st_type
        pin.type_role = adapter.type_role
        pin.native_member = adapter.native_member
        pin.native_type = adapter.native_type
        pin.adapter = adapter.adapter
        pin.resolution = (
            "unresolved_type" if adapter.st_type == "UNRESOLVED" else "mapped"
        )
    return pins


def build() -> list[Fb]:
    catalog = read_catalog()
    records, aliases = parse_records()
    adapters = parse_adapter_overlay()
    basic = parse_restricted_pin_yaml(
        ROOT / "doc/compliance/iec-basic-fb-io.yml"
    )
    part1 = parse_motion_yaml(
        ROOT / "doc/compliance/plcopen-motion-part1-io.yml", True,
        "plcopen-motion-part1-io-v1"
    )
    part4 = parse_motion_yaml(
        ROOT / "doc/compliance/plcopen-motion-part4-io.yml", False,
        "plcopen-motion-part4-io-v1"
    )
    part5 = parse_motion_yaml(
        ROOT / "doc/compliance/plcopen-motion-part5-io.yml", False,
        "plcopen-motion-part5-io-v1"
    )
    expected_part4 = {
        name for set_name, name, _ in catalog
        if set_name == "plcopen_part4"
    }
    actual_part4 = set(part4)
    if actual_part4 != expected_part4:
        missing = ", ".join(sorted(expected_part4 - actual_part4)) or "none"
        unexpected = ", ".join(sorted(actual_part4 - expected_part4)) or "none"
        raise RuntimeError(
            "plcopen_part4 YAML FB mismatch: "
            f"missing [{missing}], unexpected [{unexpected}]"
        )
    sources = {
        "iec_basic": "doc/compliance/iec-basic-fb-io.yml",
        "plcopen_part1_part2": "doc/compliance/plcopen-motion-part1-io.yml",
        "plcopen_part4": "doc/compliance/plcopen-motion-part4-io.yml",
        "plcopen_part5": "doc/compliance/plcopen-motion-part5-io.yml",
    }
    authority = {
        "iec_basic": basic,
        "plcopen_part1_part2": part1,
        "plcopen_part4": part4,
        "plcopen_part5": part5,
    }
    catalog_names = {name for _, name, _ in catalog}
    if set(adapters) != catalog_names:
        missing = ", ".join(sorted(catalog_names - set(adapters))) or "none"
        unexpected = ", ".join(sorted(set(adapters) - catalog_names)) or "none"
        raise RuntimeError(
            "adapter overlay FB mismatch: "
            f"missing [{missing}], unexpected [{unexpected}]"
        )
    result: list[Fb] = []
    for set_name, name, native in catalog:
        adapter_fb = adapters[name]
        expected_native = f"fb::{native}"
        if adapter_fb.native != expected_native:
            raise RuntimeError(
                f"{name}: adapter native {adapter_fb.native} does not match "
                f"catalog native {expected_native}"
            )
        expected_lifecycle = "cycle" if set_name == "iec_basic" else "call"
        if name in {"TON", "TOF", "TP", "MC_TorqueControl"}:
            expected_task_period = "set_cycle_time"
        elif name in {"MC_PositionProfile", "MC_VelocityProfile",
                      "MC_AccelerationProfile", "MC_DigitalCamSwitch"}:
            expected_task_period = "required"
        else:
            expected_task_period = "none"
        if adapter_fb.lifecycle != expected_lifecycle:
            raise RuntimeError(
                f"{name}: expected lifecycle {expected_lifecycle}, "
                f"got {adapter_fb.lifecycle}"
            )
        if adapter_fb.task_period != expected_task_period:
            raise RuntimeError(
                f"{name}: expected task_period {expected_task_period}, "
                f"got {adapter_fb.task_period}"
            )
        pins = bind_overlay_pins(
            name, native, authority[set_name].get(name, []), adapter_fb,
            records, aliases
        )
        resolution = "mapped" if pins else "unresolved_no_pins"
        if resolution == "mapped" and any(
            pin.resolution != "mapped" for pin in pins
        ):
            resolution = "unresolved_pins"
        result.append(Fb(set_name, name, f"fb::{native}", sources[set_name],
                         pins, resolution, adapter_fb.lifecycle,
                         adapter_fb.task_period))
    expected_pins = {
        "iec_basic": 40,
        "plcopen_part1_part2": 532,
        "plcopen_part4": 757,
        "plcopen_part5": 147,
    }
    for set_name, expected in expected_pins.items():
        actual = sum(len(fb.pins) for fb in result if fb.set_name == set_name)
        if actual != expected:
            raise RuntimeError(
                f"{set_name}: expected {expected} declared pins, got {actual}"
            )
    for fb in result:
        seen: set[str] = set()
        for pin in fb.pins:
            key = normalize(pin.name)
            if key in seen:
                raise RuntimeError(f"duplicate pin {fb.name}.{pin.name}")
            seen.add(key)
            if pin.direction not in {"input", "output", "in_out", "unresolved"}:
                raise RuntimeError(
                    f"invalid direction {pin.direction} for {fb.name}.{pin.name}"
                )
            if not pin.st_type or not pin.native_member or not pin.native_type:
                raise RuntimeError(f"incomplete pin row {fb.name}.{pin.name}")
    return result


def render(fbs: list[Fb]) -> tuple[str, dict[str, int]]:
    counts = {
        "fbs": len(fbs),
        "pins": sum(len(fb.pins) for fb in fbs),
        "resolved_fbs": sum(fb.resolution == "mapped" for fb in fbs),
        "unresolved_fbs": sum(fb.resolution != "mapped" for fb in fbs),
        "resolved_pins": sum(pin.resolution == "mapped" for fb in fbs for pin in fb.pins),
        "unresolved_pins": sum(pin.resolution != "mapped" for fb in fbs for pin in fb.pins),
    }
    for set_name in ("iec_basic", "plcopen_part1_part2", "plcopen_part4",
                     "plcopen_part5"):
        counts[f"pins_{set_name}"] = sum(
            len(fb.pins) for fb in fbs if fb.set_name == set_name
        )
        counts[f"unresolved_{set_name}"] = sum(
            pin.resolution != "mapped" for fb in fbs
            if fb.set_name == set_name for pin in fb.pins
        )
    lines = [
        "# Generated by tools/generate_st_binding_pin_catalog.py; do not edit.",
        "schema: plcopen-st-binding-pin-catalog-v2",
        "source: doc/compliance/st-binding-fb-catalog.yml",
        ("summary: {fbs: %(fbs)d, pins: %(pins)d, resolved_fbs: "
         "%(resolved_fbs)d, unresolved_fbs: %(unresolved_fbs)d, "
         "resolved_pins: %(resolved_pins)d, unresolved_pins: "
         "%(unresolved_pins)d}" % counts),
        "fbs:",
    ]
    for fb in fbs:
        lines.append(f"  - set: {fb.set_name}")
        lines.append(f"    name: {quote(fb.name)}")
        lines.append(f"    native: {quote(fb.native)}")
        lines.append(f"    source: {quote(fb.source)}")
        lines.append(f"    lifecycle: {fb.lifecycle}")
        lines.append(f"    task_period: {fb.task_period}")
        lines.append(f"    resolution: {fb.resolution}")
        lines.append("    pins:")
        if not fb.pins:
            lines.append("      []")
        for pin in fb.pins:
            lines.append(f"      - name: {quote(pin.name)}")
            lines.append(f"        direction: {pin.direction}")
            lines.append(f"        st_type: {quote(pin.st_type)}")
            lines.append(f"        type_role: {pin.type_role}")
            lines.append(f"        native_type: {quote(pin.native_type)}")
            lines.append(f"        native_member: {quote(pin.native_member)}")
            lines.append(f"        adapter: {pin.adapter}")
            lines.append(f"        resolution: {pin.resolution}")
    return "\n".join(lines) + "\n", counts


def render_gaps(fbs: list[Fb]) -> str:
    unresolved = [
        (fb, pin) for fb in fbs for pin in fb.pins
        if pin.resolution != "mapped"
    ]
    lines = [
        "# Generated by tools/generate_st_binding_pin_catalog.py; do not edit.",
        "schema: plcopen-st-binding-adapter-gaps-v2",
        "source: doc/compliance/generated/st-binding-fb-pins.yml",
        (f"summary: {{fbs: {len({fb.name for fb, _ in unresolved})}, "
         f"pins: {len(unresolved)}}}"),
        "missing_adapters:",
    ]
    for fb, pin in unresolved:
        lines.append(f"  - fb: {quote(fb.name)}")
        lines.append(f"    native: {quote(fb.native)}")
        lines.append(f"    pin: {quote(pin.name)}")
        lines.append(f"    direction: {pin.direction}")
        lines.append(f"    st_type: {quote(pin.st_type)}")
        lines.append(f"    resolution: {pin.resolution}")
        required = (
            "binding type authority + object codec"
            if pin.resolution == "unresolved_type"
            else "native_member + native_type + adapter"
        )
        lines.append(f"    required_overlay: {quote(required)}")
    return "\n".join(lines) + "\n"


def render_cpp(fbs: list[Fb]) -> str:
    """Render the complete authority as stable, dependency-free C++ metadata."""
    set_ordinals: dict[str, int] = defaultdict(int)
    fb_enum_rows: list[str] = []
    fb_rows: list[str] = []
    pin_rows: list[str] = []
    first_pin = 0
    stable_ids: set[int] = set()
    directions = {
        "input": "input",
        "output": "output",
        "in_out": "in_out",
        "unresolved": "unresolved",
    }
    enum_identifiers: set[str] = set()
    parser_keys: set[str] = set()

    for fb_index, fb in enumerate(fbs):
        enum_identifier = pin_to_snake(fb.name)
        parser_key = fb.name.lower()
        if enum_identifier in enum_identifiers:
            raise RuntimeError(f"duplicate FB enum identifier {enum_identifier}")
        if parser_key in parser_keys:
            raise RuntimeError(f"duplicate FB parser key {parser_key}")
        enum_identifiers.add(enum_identifier)
        parser_keys.add(parser_key)
        fb_enum_rows.append(f"    {enum_identifier} = {fb_index}U,")
        set_ordinals[fb.set_name] += 1
        ordinal = set_ordinals[fb.set_name]
        stable_id = (SET_STABLE_CODES[fb.set_name] << 16) | ordinal
        if stable_id in stable_ids:
            raise RuntimeError(f"duplicate stable FB id 0x{stable_id:08x}")
        stable_ids.add(stable_id)
        if len(fb.pins) > 0xffff or first_pin > 0xffff:
            raise RuntimeError("generated binding metadata exceeds uint16_t range")
        fb_rows.append(
            "    {StBindingFbType::%s, 0x%08xU, %s, %s, "
            "StBindingSet::%s, StBindingLifecycle::%s, "
            "StBindingTaskPeriod::%s, %s, %s, %dU, %dU},"
            % (enum_identifier, stable_id, cpp_quote(fb.name),
               cpp_quote(parser_key), fb.set_name, fb.lifecycle,
               fb.task_period, cpp_quote(fb.native), cpp_quote(fb.source),
               first_pin, len(fb.pins))
        )
        for pin_id, pin in enumerate(fb.pins):
            pin_rows.append(
                "    {%dU, %s, StBindingPinDirection::%s, %s, %s, %s, "
                "StBindingAdapterKind::%s},"
                % (pin_id, cpp_quote(pin.name), directions[pin.direction],
                   cpp_quote(pin.st_type), cpp_quote(pin.type_role),
                   cpp_quote(pin.native_member), pin.adapter)
            )
        first_pin += len(fb.pins)

    if first_pin > 0xffff:
        raise RuntimeError("generated binding pin metadata exceeds uint16_t range")

    lines = [
        "#pragma once",
        "",
        "// Generated by tools/generate_st_binding_pin_catalog.py; do not edit.",
        "// Stable FB ids encode the authority set in the upper 16 bits and",
        "// the one-based canonical set ordinal in the lower 16 bits.",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace plcopen::core::st::generated",
        "{",
        "",
        "enum class StBindingSet : std::uint8_t",
        "{",
        "    iec_basic = 1,",
        "    plcopen_part1_part2 = 2,",
        "    plcopen_part4 = 3,",
        "    plcopen_part5 = 4,",
        "};",
        "",
        "enum class StBindingPinDirection : std::uint8_t",
        "{",
        "    input = 0,",
        "    output = 1,",
        "    in_out = 2,",
        "    unresolved = 3,",
        "};",
        "",
        "enum class StBindingAdapterKind : std::uint8_t",
        "{",
        "    unresolved = 0,",
        "    direct = 1,",
        "    enum_cast = 2,",
        "    binding_ref = 3,",
        "    sequence = 4,",
        "    constant = 5,",
        "    tagged_reference = 6,",
        "};",
        "",
        "enum class StBindingLifecycle : std::uint8_t",
        "{",
        "    cycle = 0,",
        "    call = 1,",
        "};",
        "",
        "enum class StBindingTaskPeriod : std::uint8_t",
        "{",
        "    none = 0,",
        "    set_cycle_time = 1,",
        "    required = 2,",
        "};",
        "",
        "enum class StBindingFbType : std::uint16_t",
        "{",
        *fb_enum_rows,
        f"    count = {len(fbs)}U,",
        "};",
        "",
        "struct StBindingPinMetadata",
        "{",
        "    std::uint16_t stable_id; // zero-based canonical pin ordinal",
        "    std::string_view name;",
        "    StBindingPinDirection direction;",
        "    std::string_view st_type;",
        "    std::string_view type_role;",
        "    std::string_view native_member;",
        "    StBindingAdapterKind adapter;",
        "};",
        "",
        "struct StBindingFbMetadata",
        "{",
        "    StBindingFbType type;",
        "    std::uint32_t stable_id;",
        "    std::string_view name;",
        "    std::string_view parser_key;",
        "    StBindingSet set;",
        "    StBindingLifecycle lifecycle;",
        "    StBindingTaskPeriod task_period;",
        "    std::string_view native;",
        "    std::string_view source;",
        "    std::uint16_t first_pin;",
        "    std::uint16_t pin_count;",
        "};",
        "",
        (f"inline constexpr std::array<StBindingPinMetadata, {first_pin}> "
         "kStBindingPins = {{"),
        *pin_rows,
        "}};",
        "",
        (f"inline constexpr std::array<StBindingFbMetadata, {len(fbs)}> "
         "kStBindingFbs = {{"),
        *fb_rows,
        "}};",
        "",
        "constexpr const StBindingFbMetadata *",
        "st_binding_fb_metadata(StBindingFbType type) noexcept",
        "{",
        "    const auto index = static_cast<std::uint16_t>(type);",
        "    return index < kStBindingFbs.size() ? &kStBindingFbs[index] : nullptr;",
        "}",
        "",
        "constexpr const StBindingPinMetadata *",
        "st_binding_pin_metadata(StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    const StBindingFbMetadata *fb = st_binding_fb_metadata(type);",
        "    return fb != nullptr && pin < fb->pin_count",
        "               ? &kStBindingPins[fb->first_pin + pin]",
        "               : nullptr;",
        "}",
        "",
        "constexpr bool st_binding_pin_accessor_available(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    const StBindingPinMetadata *metadata =",
        "        st_binding_pin_metadata(type, pin);",
        "    return metadata != nullptr &&",
        "           metadata->adapter != StBindingAdapterKind::unresolved;",
        "}",
        "",
        (f"static_assert(static_cast<std::uint16_t>(StBindingFbType::count) "
         f"== {len(fbs)}U);"),
        f"static_assert(kStBindingFbs.size() == {len(fbs)}U);",
        f"static_assert(kStBindingPins.size() == {first_pin}U);",
        "",
        "} // namespace plcopen::core::st::generated",
    ]
    return "\n".join(lines) + "\n"


def render_pin_desc_cpp(fbs: list[Fb]) -> str:
    binding_types = read_binding_type_authority()
    rows: list[str] = []
    carrier_rows: list[str] = []
    type_id_rows: list[str] = []
    seen_types: set[str] = set()
    max_pin_count = 0
    for fb in fbs:
        max_pin_count = max(max_pin_count, len(fb.pins))
        for pin in fb.pins:
            carrier, type_id = pin_cpp_type(pin.st_type, binding_types)
            if pin.st_type not in seen_types:
                seen_types.add(pin.st_type)
                carrier_rows.append(
                    f"    if(name == {cpp_quote(pin.st_type)}) return {carrier};"
                )
                type_id_rows.append(
                    f"    if(name == {cpp_quote(pin.st_type)}) return {type_id};"
                )
            lower_name = pin.name.lower()
            if any("A" <= char <= "Z" for char in lower_name):
                raise RuntimeError(f"pin lower-name conversion failed: {pin.name}")
            is_input = pin.direction in {"input", "in_out"}
            rows.append(
                "    {%s, %s, %s, StBindingPinDirection::%s, %s},"
                % (cpp_quote(lower_name), carrier, type_id, pin.direction,
                   "true" if is_input else "false")
            )
    if max_pin_count > 0xffff:
        raise RuntimeError("generated FB pin table exceeds uint16_t range")
    pin_count = len(rows)
    lines = [
        "#pragma once",
        "",
        "// Generated by tools/generate_st_binding_pin_catalog.py; do not edit.",
        "// Aggregate/custom identity is authoritative in TypeId; Type is the",
        "// scalar carrier used by the existing expression contract.",
        "",
        "namespace generated",
        "{",
        "",
        (f"inline constexpr std::array<PinDesc, {pin_count}> "
         "kStBindingPinDescs = {{"),
        *rows,
        "}};",
        "",
        "constexpr PinTable st_binding_pin_table(",
        "    StBindingFbType type) noexcept",
        "{",
        "    const StBindingFbMetadata *metadata =",
        "        st_binding_fb_metadata(type);",
        "    return metadata == nullptr",
        "               ? PinTable{}",
        "               : PinTable{kStBindingPinDescs.data() +",
        "                              metadata->first_pin,",
        "                          metadata->pin_count};",
        "}",
        "",
        "constexpr char st_binding_ascii_lower(char value) noexcept",
        "{",
        "    return value >= 'A' && value <= 'Z'",
        "               ? static_cast<char>(value - 'A' + 'a')",
        "               : value;",
        "}",
        "",
        "constexpr bool st_binding_lower_name_matches(",
        "    std::string_view lower, std::string_view canonical) noexcept",
        "{",
        "    if(lower.size() != canonical.size()) return false;",
        "    for(std::size_t index = 0; index < lower.size(); ++index)",
        "        if(lower[index] != st_binding_ascii_lower(canonical[index]))",
        "            return false;",
        "    return true;",
        "}",
        "",
        "constexpr Type st_binding_pin_type_for_name(",
        "    std::string_view name) noexcept",
        "{",
        *carrier_rows,
        "    return Type::bool_;",
        "}",
        "",
        "constexpr TypeId st_binding_pin_type_id_for_name(",
        "    std::string_view name) noexcept",
        "{",
        *type_id_rows,
        "    return invalid_type_id;",
        "}",
        "",
        "constexpr bool st_binding_pin_descs_valid() noexcept",
        "{",
        "    if(kStBindingPinDescs.size() != kStBindingPins.size()) return false;",
        "    for(std::size_t index = 0; index < kStBindingPinDescs.size();",
        "        ++index) {",
        "        const PinDesc &desc = kStBindingPinDescs[index];",
        "        const StBindingPinMetadata &metadata = kStBindingPins[index];",
        "        if(!st_binding_lower_name_matches(desc.lower_name,",
        "                                          metadata.name)) return false;",
        "        if(desc.direction != metadata.direction) return false;",
        "        if(desc.type !=",
        "           st_binding_pin_type_for_name(metadata.st_type)) return false;",
        "        if(desc.type_id !=",
        "           st_binding_pin_type_id_for_name(metadata.st_type)) return false;",
        "        const bool accepts_input =",
        "            metadata.direction == StBindingPinDirection::input ||",
        "            metadata.direction == StBindingPinDirection::in_out;",
        "        if(desc.is_input != accepts_input) return false;",
        "        const bool unresolved_type = metadata.st_type == \"UNRESOLVED\";",
        "        if((desc.type_id == invalid_type_id) != unresolved_type)",
        "            return false;",
        "        if(metadata.direction == StBindingPinDirection::unresolved)",
        "            return false;",
        "    }",
        "    return true;",
        "}",
        "",
        "constexpr bool st_binding_pin_tables_valid() noexcept",
        "{",
        "    for(const StBindingFbMetadata &metadata : kStBindingFbs) {",
        "        const PinTable table = st_binding_pin_table(metadata.type);",
        "        if(table.count != metadata.pin_count) return false;",
        "        if(table.pins !=",
        "           kStBindingPinDescs.data() + metadata.first_pin) return false;",
        "    }",
        "    return st_binding_pin_table(StBindingFbType::count).pins == nullptr;",
        "}",
        "",
        f"static_assert(kStBindingPinDescs.size() == {pin_count}U);",
        f"static_assert(kStBindingFbs.size() == {len(fbs)}U);",
        "static_assert(st_binding_pin_descs_valid());",
        "static_assert(st_binding_pin_tables_valid());",
        "",
        "} // namespace generated",
    ]
    return "\n".join(lines) + "\n"


PRIMITIVE_OBJECT_LAYOUTS = {
    "BOOL": (1, 1),
    "DINT": (4, 4),
    "ULINT": (8, 8),
    "LREAL": (8, 8),
}


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def object_type_layout(spec: object,
                       binding_types: dict[str, dict[str, object]]) -> tuple[int, int]:
    if isinstance(spec, str):
        if spec in PRIMITIVE_OBJECT_LAYOUTS:
            return PRIMITIVE_OBJECT_LAYOUTS[spec]
        metadata = binding_types.get(spec)
        if metadata is None:
            raise RuntimeError(f"object codec type absent from authority: {spec}")
        return int(metadata["size"]), int(metadata["alignment"])
    if isinstance(spec, dict) and "array" in spec:
        element_size, element_alignment = object_type_layout(
            spec["array"], binding_types
        )
        return element_size * int(spec["count"]), element_alignment
    raise RuntimeError(f"unsupported object codec field type: {spec!r}")


def object_field_layouts(fields: list[dict[str, object]], expected_size: int,
                         binding_types: dict[str, dict[str, object]]) -> list[tuple[dict[str, object], int]]:
    result: list[tuple[dict[str, object], int]] = []
    cursor = 0
    max_alignment = 1
    for field in fields:
        size, alignment = object_type_layout(field["type"], binding_types)
        cursor = align_up(cursor, alignment)
        result.append((field, cursor))
        cursor += size
        max_alignment = max(max_alignment, alignment)
    if align_up(cursor, max_alignment) != expected_size:
        raise RuntimeError(
            f"object codec field layout drift: expected {expected_size}, "
            f"computed {align_up(cursor, max_alignment)}"
        )
    return result


def object_primitive_lines(st_type: str, offset: int, native_expr: str,
                           decode: bool) -> list[str]:
    if decode:
        if st_type == "BOOL":
            return [
                "    {",
                ("        const std::uint8_t raw = "
                 f"st_binding_object_read<std::uint8_t>(bytes, {offset}U);"),
                "        if(raw > 1U) return false;",
                f"        {native_expr} = raw != 0U;",
                "    }",
            ]
        if st_type == "LREAL":
            return [
                (f"    {native_expr} = st_binding_object_read<double>("
                 f"bytes, {offset}U);")
            ]
        if st_type == "ULINT":
            return [
                "    {",
                ("        const std::uint64_t raw = "
                 f"st_binding_object_read<std::uint64_t>(bytes, {offset}U);"),
                ("        using Native = std::remove_reference_t<decltype("
                 f"{native_expr})>;"),
                ("        if(raw > static_cast<std::uint64_t>("
                 "(std::numeric_limits<Native>::max)())) return false;"),
                f"        {native_expr} = static_cast<Native>(raw);",
                "    }",
            ]
        if st_type == "DINT":
            return [
                "    {",
                ("        const std::int32_t raw = "
                 f"st_binding_object_read<std::int32_t>(bytes, {offset}U);"),
                ("        using Native = std::remove_reference_t<decltype("
                 f"{native_expr})>;"),
                ("        if(raw < static_cast<std::int64_t>("
                 "(std::numeric_limits<Native>::min)()) ||"),
                ("           raw > static_cast<std::int64_t>("
                 "(std::numeric_limits<Native>::max)())) return false;"),
                f"        {native_expr} = static_cast<Native>(raw);",
                "    }",
            ]
    else:
        if st_type == "BOOL":
            return [
                ("    st_binding_object_write<std::uint8_t>(bytes, "
                 f"{offset}U, {native_expr} ? 1U : 0U);")
            ]
        if st_type == "LREAL":
            return [
                ("    st_binding_object_write<double>(bytes, "
                 f"{offset}U, static_cast<double>({native_expr}));")
            ]
        if st_type == "ULINT":
            return [
                "    {",
                f"        const auto native = {native_expr};",
                "        using Native = std::remove_cv_t<decltype(native)>;",
                ("        if constexpr(sizeof(native) > "
                 "sizeof(std::uint64_t)) {"),
                ("            if(native > static_cast<Native>("
                 "(std::numeric_limits<std::uint64_t>::max)())) return false;"),
                "        }",
                ("        st_binding_object_write<std::uint64_t>(bytes, "
                 f"{offset}U, static_cast<std::uint64_t>(native));"),
                "    }",
            ]
        if st_type == "DINT":
            return [
                "    {",
                f"        const auto native = {native_expr};",
                "        using Native = std::remove_cv_t<decltype(native)>;",
                ("        if(native < static_cast<Native>("
                 "(std::numeric_limits<std::int32_t>::min)()) ||"),
                ("           native > static_cast<Native>("
                 "(std::numeric_limits<std::int32_t>::max)())) return false;"),
                ("        st_binding_object_write<std::int32_t>(bytes, "
                 f"{offset}U, static_cast<std::int32_t>(native));"),
                "    }",
            ]
    raise RuntimeError(f"unsupported object codec primitive {st_type}")


def object_value_lines(spec: object, offset: int, native_expr: str,
                       decode: bool,
                       binding_types: dict[str, dict[str, object]]) -> list[str]:
    if isinstance(spec, str) and spec in PRIMITIVE_OBJECT_LAYOUTS:
        return object_primitive_lines(spec, offset, native_expr, decode)
    if isinstance(spec, dict) and "array" in spec:
        result: list[str] = []
        element_size, _ = object_type_layout(spec["array"], binding_types)
        for index in range(int(spec["count"])):
            result.extend(object_value_lines(
                spec["array"], offset + index * element_size,
                f"{native_expr}[{index}]", decode, binding_types
            ))
        return result
    if isinstance(spec, str):
        metadata = binding_types[spec]
        fields = metadata.get("fields")
        if not isinstance(fields, list):
            raise RuntimeError(f"{spec}: object codec requires fields")
        result = []
        for field, field_offset in object_field_layouts(
            fields, int(metadata["size"]), binding_types
        ):
            result.extend(object_value_lines(
                field["type"], offset + field_offset,
                f"{native_expr}.{field['native']}", decode, binding_types
            ))
        return result
    raise RuntimeError(f"unsupported object codec value {spec!r}")


def render_object_codec_helpers(
    binding_types: dict[str, dict[str, object]]
) -> list[str]:
    lines: list[str] = []
    for st_type in sorted(OBJECT_CODEC_TYPES):
        metadata = binding_types.get(st_type)
        if metadata is None:
            raise RuntimeError(f"missing object codec authority {st_type}")
        size = int(metadata["size"])
        native = str(metadata["native"])
        codec = str(metadata["codec"])
        if codec == "fields":
            body_decode = object_value_lines(
                st_type, 0, "value", True, binding_types
            )
            body_encode = object_value_lines(
                st_type, 0, "value", False, binding_types
            )
            if st_type == "MC_GROUP_POSITION":
                body_decode.append("    if(value.size > 8U) return false;")
                body_encode.insert(0, "    if(value.size > 8U) return false;")
        elif codec in {"fixed-array", "counted-array"}:
            capacity = int(metadata["capacity"])
            element = metadata["element"]
            if isinstance(element, dict):
                element_size = int(element["size"])
                element_fields = element.get("fields")
                if not isinstance(element_fields, list):
                    raise RuntimeError(f"{st_type}: element fields missing")
                element_layout = object_field_layouts(
                    element_fields, element_size, binding_types
                )
                body_decode = []
                body_encode = []
                container = (
                    "value.value" if codec == "counted-array" else "value"
                )
                for index in range(capacity):
                    for field, field_offset in element_layout:
                        expression = f"{container}[{index}].{field['native']}"
                        body_decode.extend(object_value_lines(
                            field["type"], index * element_size + field_offset,
                            expression, True, binding_types
                        ))
                        body_encode.extend(object_value_lines(
                            field["type"], index * element_size + field_offset,
                            expression, False, binding_types
                        ))
            else:
                element_size, _ = object_type_layout(element, binding_types)
                body_decode = []
                body_encode = []
                container = "value.value" if codec == "counted-array" else "value"
                for index in range(capacity):
                    body_decode.extend(object_value_lines(
                        element, index * element_size,
                        f"{container}[{index}]", True, binding_types
                    ))
                    body_encode.extend(object_value_lines(
                        element, index * element_size,
                        f"{container}[{index}]", False, binding_types
                    ))
            payload_size = capacity * element_size
            if codec == "counted-array":
                count = metadata.get("count")
                if not isinstance(count, dict):
                    raise RuntimeError(f"{st_type}: count metadata missing")
                count_size, count_alignment = object_type_layout(
                    count["type"], binding_types
                )
                count_offset = align_up(payload_size, count_alignment)
                if align_up(count_offset + count_size,
                            int(metadata["alignment"])) != size:
                    raise RuntimeError(f"{st_type}: counted-array layout drift")
                count_expr = f"value.{count['native']}"
                body_decode.extend(object_value_lines(
                    count["type"], count_offset, count_expr, True,
                    binding_types
                ))
                body_decode.append(
                    f"    if({count_expr} > {capacity}U) return false;"
                )
                body_encode.insert(
                    0, f"    if({count_expr} > {capacity}U) return false;"
                )
                body_encode.extend(object_value_lines(
                    count["type"], count_offset, count_expr, False,
                    binding_types
                ))
            elif payload_size != size:
                raise RuntimeError(f"{st_type}: fixed-array layout drift")
        else:
            raise RuntimeError(f"{st_type}: unsupported object codec {codec}")
        identifier = pin_to_snake(st_type)
        lines.extend([
            f"inline bool st_binding_decode_{identifier}(",
            f"    const unsigned char *bytes, {native} &value) noexcept",
            "{",
            *body_decode,
            "    return true;",
            "}",
            "",
            f"inline bool st_binding_encode_{identifier}(",
            f"    const {native} &value, unsigned char *bytes) noexcept",
            "{",
            *body_encode,
            "    return true;",
            "}",
            "",
        ])
    return lines


def scalar_store_body(pin: Pin, accessor: str,
                      binding_types: dict[str, dict[str, object]],
                      native_enums: dict[str, list[int]]) -> list[str]:
    lines = [
        f"        using Accessor = {accessor};",
        "        auto &instance = *static_cast<Accessor::fb_type *>(storage);",
        "        auto &target = Accessor::get(instance);",
    ]
    metadata = binding_types.get(pin.st_type, {})
    if metadata.get("kind") == "enum":
        lines.append("        switch(static_cast<std::int64_t>(bits)) {")
        for value in metadata.get("values", []):
            lines.append(
                f"        case {value['iec']}: target = static_cast<Accessor::value_type>({value['native']}); return true;"
            )
        lines.extend(["        default: return false;", "        }"])
        return lines
    enum_name = pin.native_type.split("::")[-1]
    if enum_name in native_enums:
        lines.append("        switch(static_cast<std::int64_t>(bits)) {")
        for value in native_enums[enum_name]:
            lines.append(
                f"        case {value}: target = static_cast<Accessor::value_type>({value}); return true;"
            )
        lines.extend(["        default: return false;", "        }"])
        return lines
    if "*" in pin.native_type:
        lines.extend([
            "        target = reinterpret_cast<Accessor::value_type>(",
            "            static_cast<std::uintptr_t>(bits));",
            "        return true;",
        ])
    elif pin.st_type in FLOAT_PIN_TYPES:
        lines.extend([
            "        target = static_cast<Accessor::value_type>(",
            "            st_binding_double_from_bits(bits));",
            "        return true;",
        ])
    elif pin.st_type == "BOOL":
        lines.extend(["        target = bits != 0U;", "        return true;"])
    elif pin.st_type in UNSIGNED_PIN_TYPES:
        lines.extend([
            "        target = static_cast<Accessor::value_type>(bits);",
            "        return true;",
        ])
    else:
        lines.extend([
            "        target = static_cast<Accessor::value_type>(",
            "            static_cast<std::int64_t>(bits));",
            "        return true;",
        ])
    return lines


def scalar_load_body(pin: Pin, accessor: str,
                     binding_types: dict[str, dict[str, object]]) -> list[str]:
    lines = [f"        using Accessor = {accessor};"]
    if pin.adapter == "constant":
        lines.append("        const auto value = Accessor::load();")
    else:
        lines.extend([
            "        const auto &instance =",
            "            *static_cast<const Accessor::fb_type *>(storage);",
            "        const auto &value = Accessor::get(instance);",
        ])
    metadata = binding_types.get(pin.st_type, {})
    if metadata.get("kind") == "enum":
        lines.append("        switch(static_cast<std::int64_t>(value)) {")
        for value in metadata.get("values", []):
            lines.extend([
                f"        case {value['native']}:",
                ("            bits = static_cast<std::uint64_t>("
                 f"static_cast<std::int64_t>({value['iec']}));"),
                "            return true;",
            ])
        lines.extend(["        default: return false;", "        }"])
        return lines
    if "*" in pin.native_type:
        lines.extend([
            "        bits = static_cast<std::uint64_t>(",
            "            reinterpret_cast<std::uintptr_t>(value));",
            "        return true;",
        ])
    elif pin.st_type in FLOAT_PIN_TYPES:
        lines.extend([
            "        bits = st_binding_double_bits(static_cast<double>(value));",
            "        return true;",
        ])
    elif pin.st_type == "BOOL":
        lines.extend(["        bits = value ? 1U : 0U;", "        return true;"])
    elif pin.st_type in UNSIGNED_PIN_TYPES:
        lines.extend([
            "        bits = static_cast<std::uint64_t>(value);",
            "        return true;",
        ])
    else:
        lines.extend([
            "        bits = static_cast<std::uint64_t>(",
            "            static_cast<std::int64_t>(value));",
            "        return true;",
        ])
    return lines


def render_native_cpp(fbs: list[Fb]) -> str:
    binding_types = read_binding_type_authority()
    tagged_reference_types = {
        name for name, metadata in binding_types.items()
        if metadata.get("codec") == "tagged-reference"
    }
    if tagged_reference_types != TAGGED_REFERENCE_CODEC_TYPES:
        raise RuntimeError(
            "tagged-reference codec authority drift: "
            f"expected {sorted(TAGGED_REFERENCE_CODEC_TYPES)}, "
            f"got {sorted(tagged_reference_types)}"
        )
    kin_transform_metadata = binding_types["MC_KIN_TRANSFORM_REF"]
    if kin_transform_metadata.get("tags") != [
        {"name": "none", "value": 0},
        {"name": "kinematics", "value": 1},
        {"name": "pose", "value": 2},
    ]:
        raise RuntimeError("MC_KIN_TRANSFORM_REF tag authority drift")
    native_enums = parse_native_enum_values()
    includes = [
        path.relative_to(ROOT / "core").as_posix()
        for path in sorted((ROOT / "core/fb").glob("*.h"))
    ]
    size_cases: list[str] = []
    align_cases: list[str] = []
    construct_cases: list[str] = []
    destruct_cases: list[str] = []
    invoke_cases: list[str] = []
    accessor_rows: list[str] = []
    construct_assertions: list[str] = []
    capability_cases: list[str] = []
    store_cases: list[str] = []
    load_cases: list[str] = []
    store_object_cases: list[str] = []
    load_object_cases: list[str] = []
    store_sequence_cases: list[str] = []
    load_sequence_cases: list[str] = []
    store_tagged_reference_cases: list[str] = []
    load_tagged_reference_cases: list[str] = []
    store_scalar_count = 0
    load_scalar_count = 0
    store_object_count = 0
    load_object_count = 0
    store_sequence_count = 0
    load_sequence_count = 0
    store_tagged_reference_count = 0
    load_tagged_reference_count = 0

    for fb in fbs:
        enum_identifier = pin_to_snake(fb.name)
        enum_value = f"StBindingFbType::{enum_identifier}"
        native = fb.native
        size_cases.append(f"    case {enum_value}: return sizeof({native});")
        align_cases.append(f"    case {enum_value}: return alignof({native});")
        construct_cases.extend([
            f"    case {enum_value}: {{",
            f"        using Native = {native};",
            "        ::new (storage) Native();",
            "        return true;",
            "    }",
        ])
        destruct_cases.extend([
            f"    case {enum_value}: {{",
            f"        using Native = {native};",
            "        static_cast<Native *>(storage)->~Native();",
            "        return true;",
            "    }",
        ])
        invoke_cases.extend([
            f"    case {enum_value}: {{",
            f"        auto &instance = *static_cast<{native} *>(storage);",
        ])
        if fb.task_period == "set_cycle_time":
            invoke_cases.append(
                "        if(!instance.set_cycle_time(task_period)) return false;"
            )
        elif fb.task_period == "required":
            invoke_cases.append("        if(task_period <= 0) return false;")
        if fb.name == "MC_DigitalCamSwitch":
            invoke_cases.append("        instance.call(task_period);")
        else:
            invoke_cases.append(f"        instance.{fb.lifecycle}();")
        invoke_cases.extend([
            "        return true;",
            "    }",
        ])
        construct_assertions.append(
            f"static_assert(std::is_default_constructible_v<{native}>);"
        )
        construct_assertions.append(
            f"static_assert(std::is_destructible_v<{native}>);"
        )

        if len(fb.pins) > 64:
            raise RuntimeError(f"{fb.name}: scalar capability mask exceeds 64 pins")
        store_mask = 0
        load_mask = 0
        store_object_mask = 0
        load_object_mask = 0
        store_sequence_mask = 0
        load_sequence_mask = 0
        store_tagged_reference_mask = 0
        load_tagged_reference_mask = 0
        fb_store_cases: list[str] = []
        fb_load_cases: list[str] = []
        fb_store_object_cases: list[str] = []
        fb_load_object_cases: list[str] = []
        fb_store_sequence_cases: list[str] = []
        fb_load_sequence_cases: list[str] = []
        fb_store_tagged_reference_cases: list[str] = []
        fb_load_tagged_reference_cases: list[str] = []

        for pin_id, pin in enumerate(fb.pins):
            if (pin.st_type in OBJECT_CODEC_TYPES and
                    pin.adapter != "direct"):
                raise RuntimeError(
                    f"{fb.name}.{pin.name}: object codec requires direct "
                    f"adapter, got {pin.adapter}"
                )
            if (pin.st_type in SEQUENCE_CODEC_TYPES and
                    pin.adapter not in {"direct", "sequence"}):
                raise RuntimeError(
                    f"{fb.name}.{pin.name}: sequence codec requires direct "
                    f"or sequence adapter, got {pin.adapter}"
                )
            if (pin.st_type in TAGGED_REFERENCE_CODEC_TYPES and
                    pin.adapter != "tagged_reference"):
                raise RuntimeError(
                    f"{fb.name}.{pin.name}: tagged-reference codec requires "
                    f"tagged_reference adapter, got {pin.adapter}"
                )
            if pin.adapter == "unresolved":
                continue
            specialization = (
                "StBindingNativePinAccessor<"
                f"{enum_value}, {pin_id}U>"
            )
            accessor_rows.extend([
                "template <>",
                f"struct {specialization}",
                "{",
                "    static constexpr bool available = true;",
                # A validated member path is not by itself a runtime codec.
                "    static constexpr bool registered = false;",
                ("    static constexpr StBindingAdapterKind kind = "
                 f"StBindingAdapterKind::{pin.adapter};"),
                f"    using fb_type = {native};",
            ])
            if pin.adapter == "constant":
                accessor_rows.extend([
                    f"    using value_type = {pin.native_type};",
                    ("    static constexpr value_type load() noexcept "
                     f"{{ return {pin.native_member}; }}"),
                ])
            elif pin.adapter == "sequence":
                data_member, count_member = pin.native_member.split(" + ")
                accessor_rows.extend([
                    ("    using data_type = std::remove_reference_t<decltype("
                     f"std::declval<fb_type &>().{data_member})>;"),
                    ("    using count_type = std::remove_reference_t<decltype("
                     f"std::declval<fb_type &>().{count_member})>;"),
                    ("    static data_type &data(fb_type &instance) noexcept "
                     f"{{ return instance.{data_member}; }}"),
                    ("    static const data_type &data(const fb_type &instance) "
                     f"noexcept {{ return instance.{data_member}; }}"),
                    ("    static count_type &count(fb_type &instance) noexcept "
                     f"{{ return instance.{count_member}; }}"),
                    ("    static const count_type &count(const fb_type &instance) "
                     f"noexcept {{ return instance.{count_member}; }}"),
                ])
            else:
                accessor_rows.extend([
                    ("    using value_type = std::remove_reference_t<decltype("
                     f"std::declval<fb_type &>().{pin.native_member})>;"),
                    ("    static value_type &get(fb_type &instance) noexcept "
                     f"{{ return instance.{pin.native_member}; }}"),
                    ("    static const value_type &get(const fb_type &instance) "
                     f"noexcept {{ return instance.{pin.native_member}; }}"),
                ])
            accessor_rows.extend(["};", ""])

            if pin.st_type in OBJECT_CODEC_TYPES:
                metadata = binding_types[pin.st_type]
                type_id = f"binding_type::{pin_to_snake(pin.st_type)}"
                object_size = int(metadata["size"])
                accessor = (
                    "StBindingNativePinAccessor<"
                    f"{enum_value}, {pin_id}U>"
                )
                helper = pin_to_snake(pin.st_type)
                if pin.direction in {"input", "in_out"}:
                    store_object_mask |= 1 << pin_id
                    store_object_count += 1
                    fb_store_object_cases.extend([
                        f"    case {pin_id}U: {{",
                        f"        using Accessor = {accessor};",
                        (f"        if(type_id != {type_id} || "
                         f"size != {object_size}U) return false;"),
                        "        Accessor::value_type decoded{};",
                        (f"        if(!st_binding_decode_{helper}(bytes, "
                         "decoded)) return false;"),
                        ("        auto &instance = "
                         "*static_cast<Accessor::fb_type *>(storage);"),
                        "        Accessor::get(instance) = decoded;",
                        "        return true;",
                        "    }",
                    ])
                if pin.direction in {"output", "in_out"}:
                    load_object_mask |= 1 << pin_id
                    load_object_count += 1
                    fb_load_object_cases.extend([
                        f"    case {pin_id}U: {{",
                        f"        using Accessor = {accessor};",
                        (f"        if(type_id != {type_id} || "
                         f"size != {object_size}U) return false;"),
                        ("        const auto &instance = *static_cast<const "
                         "Accessor::fb_type *>(storage);"),
                        (f"        std::array<unsigned char, {object_size}U> "
                         "encoded{};"),
                        (f"        if(!st_binding_encode_{helper}("
                         "Accessor::get(instance), encoded.data())) "
                         "return false;"),
                        ("        std::memcpy(bytes, encoded.data(), "
                         "encoded.size());"),
                        "        return true;",
                        "    }",
                    ])

            if pin.st_type in SEQUENCE_CODEC_TYPES:
                metadata = binding_types[pin.st_type]
                minimum = int(metadata.get("min_count", 0))
                maximum = int(metadata.get("max_count", 0))
                accessor = (
                    "StBindingNativePinAccessor<"
                    f"{enum_value}, {pin_id}U>"
                )
                store_body = [f"        using Accessor = {accessor};"]
                load_body = [f"        using Accessor = {accessor};"]
                if pin.st_type in {"MC_PATH_TABLE", "MC_PATH_DESCRIPTION"}:
                    store_body.extend([
                        ("        auto &instance = "
                         "*static_cast<Accessor::fb_type *>(storage);"),
                        ("        if(value.data == nullptr && value.count == 0U "
                         "&& !value.flag) {"),
                        "            Accessor::get(instance) = nullptr;",
                        "            return true;",
                        "        }",
                        ("        if(value.data == nullptr || "
                         "value.count != 1U || value.flag) return false;"),
                        ("        using Element = std::remove_cv_t<"
                         "std::remove_pointer_t<Accessor::value_type>>;"),
                        ("        Accessor::get(instance) = const_cast<Element *>("
                         "static_cast<const Element *>(value.data));"),
                        "        return true;",
                    ])
                    load_body.extend([
                        ("        const auto &instance = *static_cast<const "
                         "Accessor::fb_type *>(storage);"),
                        "        const auto *data = Accessor::get(instance);",
                        "        if(data == nullptr) {",
                        "            value = {};",
                        "            return true;",
                        "        }",
                        "        value = {data, 1U, false};",
                        "        return true;",
                    ])
                elif pin.st_type in {
                        "MC_CAM_SWITCH_TABLE_VIEW",
                        "MC_CAM_TRACK_OPTIONS_VIEW"}:
                    store_body.extend([
                        ("        auto &instance = "
                         "*static_cast<Accessor::fb_type *>(storage);"),
                        "        auto &target = Accessor::get(instance);",
                        ("        if(value.data == nullptr && value.count == 0U "
                         "&& !value.flag) {"),
                        "            target = {};",
                        "            return true;",
                        "        }",
                        (f"        if(value.data == nullptr || value.count < "
                         f"{minimum}U || value.count > {maximum}U || "
                         "value.flag) return false;"),
                        ("        using Element = std::remove_cv_t<"
                         "std::remove_pointer_t<decltype(target.data)>>;"),
                        ("        target = {static_cast<const Element *>("
                         "value.data), value.count};"),
                        "        return true;",
                    ])
                    load_body.extend([
                        ("        const auto &instance = *static_cast<const "
                         "Accessor::fb_type *>(storage);"),
                        "        const auto &source = Accessor::get(instance);",
                        "        if(source.data == nullptr && source.size == 0U) {",
                        "            value = {};",
                        "            return true;",
                        "        }",
                        (f"        if(source.data == nullptr || source.size < "
                         f"{minimum}U || source.size > {maximum}U) "
                         "return false;"),
                        "        value = {source.data, source.size, false};",
                        "        return true;",
                    ])
                elif pin.st_type == "MC_CAM_SWITCH_OUTPUTS_VIEW":
                    store_body.extend([
                        ("        auto &instance = "
                         "*static_cast<Accessor::fb_type *>(storage);"),
                        "        auto &target = Accessor::get(instance);",
                        ("        if(value.data == nullptr && value.count == 0U "
                         "&& !value.flag) {"),
                        "            target = {};",
                        "            return true;",
                        "        }",
                        (f"        if(value.data == nullptr || value.count < "
                         f"{minimum}U || value.count > {maximum}U || "
                         "value.flag) return false;"),
                        ("        using Element = std::remove_cv_t<"
                         "std::remove_pointer_t<decltype(target.data)>>;"),
                        ("        target = {const_cast<Element *>("
                         "static_cast<const Element *>(value.data)), "
                         "value.count};"),
                        "        return true;",
                    ])
                    load_body.extend([
                        ("        const auto &instance = *static_cast<const "
                         "Accessor::fb_type *>(storage);"),
                        "        const auto &source = Accessor::get(instance);",
                        "        if(source.data == nullptr && source.size == 0U) {",
                        "            value = {};",
                        "            return true;",
                        "        }",
                        (f"        if(source.data == nullptr || source.size < "
                         f"{minimum}U || source.size > {maximum}U) "
                         "return false;"),
                        "        value = {source.data, source.size, false};",
                        "        return true;",
                    ])
                elif pin.st_type == "MC_CAM_TABLE_VIEW":
                    store_body.extend([
                        ("        auto &instance = "
                         "*static_cast<Accessor::fb_type *>(storage);"),
                        "        auto &target = Accessor::get(instance);",
                        ("        if(value.data == nullptr && value.count == 0U "
                         "&& !value.flag) {"),
                        "            target = {};",
                        "            return true;",
                        "        }",
                        (f"        if(value.data == nullptr || value.count < "
                         f"{minimum}U || value.count > {maximum}U) "
                         "return false;"),
                        ("        using Element = std::remove_cv_t<"
                         "std::remove_pointer_t<decltype(target.points)>>;"),
                        ("        target = {static_cast<const Element *>("
                         "value.data), value.count, value.flag};"),
                        "        return true;",
                    ])
                    load_body.extend([
                        ("        const auto &instance = *static_cast<const "
                         "Accessor::fb_type *>(storage);"),
                        "        const auto &source = Accessor::get(instance);",
                        "        if(source.points == nullptr && source.size == 0U) {",
                        "            value = {};",
                        "            return true;",
                        "        }",
                        (f"        if(source.points == nullptr || source.size < "
                         f"{minimum}U || source.size > {maximum}U) "
                         "return false;"),
                        ("        value = {source.points, source.size, "
                         "source.periodic};"),
                        "        return true;",
                    ])
                else:
                    store_body.extend([
                        ("        auto &instance = "
                         "*static_cast<Accessor::fb_type *>(storage);"),
                        ("        if(value.data == nullptr && value.count == 0U "
                         "&& !value.flag) {"),
                        "            Accessor::data(instance) = nullptr;",
                        "            Accessor::count(instance) = 0U;",
                        "            return true;",
                        "        }",
                        (f"        if(value.data == nullptr || value.count < "
                         f"{minimum}U || value.count > {maximum}U || "
                         "value.flag) return false;"),
                        ("        using Element = std::remove_cv_t<"
                         "std::remove_pointer_t<Accessor::data_type>>;"),
                        ("        Accessor::data(instance) = "
                         "static_cast<const Element *>(value.data);"),
                        "        Accessor::count(instance) = value.count;",
                        "        return true;",
                    ])
                    load_body.extend([
                        ("        const auto &instance = *static_cast<const "
                         "Accessor::fb_type *>(storage);"),
                        "        const auto *data = Accessor::data(instance);",
                        "        const std::size_t count = Accessor::count(instance);",
                        "        if(data == nullptr && count == 0U) {",
                        "            value = {};",
                        "            return true;",
                        "        }",
                        (f"        if(data == nullptr || count < {minimum}U || "
                         f"count > {maximum}U) return false;"),
                        "        value = {data, count, false};",
                        "        return true;",
                    ])
                if pin.direction in {"input", "in_out"}:
                    store_sequence_mask |= 1 << pin_id
                    store_sequence_count += 1
                    fb_store_sequence_cases.extend([
                        f"    case {pin_id}U: {{", *store_body, "    }"
                    ])
                if pin.direction in {"output", "in_out"}:
                    load_sequence_mask |= 1 << pin_id
                    load_sequence_count += 1
                    fb_load_sequence_cases.extend([
                        f"    case {pin_id}U: {{", *load_body, "    }"
                    ])

            if pin.st_type in TAGGED_REFERENCE_CODEC_TYPES:
                type_id = f"binding_type::{pin_to_snake(pin.st_type)}"
                accessor = (
                    "StBindingNativePinAccessor<"
                    f"{enum_value}, {pin_id}U>"
                )
                if pin.direction in {"input", "in_out"}:
                    store_tagged_reference_mask |= 1 << pin_id
                    store_tagged_reference_count += 1
                    fb_store_tagged_reference_cases.extend([
                        f"    case {pin_id}U: {{",
                        f"        using Accessor = {accessor};",
                        f"        if(type_id != {type_id}) return false;",
                        "        Accessor::value_type decoded{};",
                        "        switch(value.tag) {",
                        "        case StBindingNativeTaggedReferenceTag::none:",
                        "            if(value.data != nullptr) return false;",
                        "            break;",
                        "        case StBindingNativeTaggedReferenceTag::kinematics:",
                        "            if(value.data == nullptr) return false;",
                        "            decoded.kind = axis::KinTransformKind::kinematics;",
                        ("            decoded.kinematics = static_cast<const "
                         "kin::Kinematics *>(value.data);"),
                        "            break;",
                        "        case StBindingNativeTaggedReferenceTag::pose:",
                        "            if(value.data == nullptr) return false;",
                        "            decoded.kind = axis::KinTransformKind::pose;",
                        ("            decoded.pose = static_cast<const "
                         "kin::PoseKinematics *>(value.data);"),
                        "            break;",
                        "        default:",
                        "            return false;",
                        "        }",
                        ("        auto &instance = "
                         "*static_cast<Accessor::fb_type *>(storage);"),
                        "        Accessor::get(instance) = decoded;",
                        "        return true;",
                        "    }",
                    ])
                if pin.direction in {"output", "in_out"}:
                    load_tagged_reference_mask |= 1 << pin_id
                    load_tagged_reference_count += 1
                    fb_load_tagged_reference_cases.extend([
                        f"    case {pin_id}U: {{",
                        f"        using Accessor = {accessor};",
                        f"        if(type_id != {type_id}) return false;",
                        ("        const auto &instance = *static_cast<const "
                         "Accessor::fb_type *>(storage);"),
                        "        const auto &source = Accessor::get(instance);",
                        "        StBindingNativeTaggedReferenceValue decoded{};",
                        "        switch(source.kind) {",
                        "        case axis::KinTransformKind::none:",
                        ("            if(source.kinematics != nullptr || "
                         "source.pose != nullptr) return false;"),
                        "            break;",
                        "        case axis::KinTransformKind::kinematics:",
                        ("            if(source.kinematics == nullptr || "
                         "source.pose != nullptr) return false;"),
                        "            decoded = {",
                        "                source.kinematics,",
                        ("                StBindingNativeTaggedReferenceTag::"
                         "kinematics};"),
                        "            break;",
                        "        case axis::KinTransformKind::pose:",
                        ("            if(source.pose == nullptr || "
                         "source.kinematics != nullptr) return false;"),
                        "            decoded = {source.pose,",
                        ("                StBindingNativeTaggedReferenceTag::"
                         "pose};"),
                        "            break;",
                        "        default:",
                        "            return false;",
                        "        }",
                        "        value = decoded;",
                        "        return true;",
                        "    }",
                    ])

            scalar_kind = scalar_binding_kind(pin.st_type, binding_types)
            scalar = bool(scalar_kind) and pin.adapter in {
                "direct", "enum_cast", "binding_ref", "constant"
            }
            if scalar_kind in {"host-ref", "ref"}:
                scalar = pin.adapter == "binding_ref"
            if not scalar:
                continue
            accessor = (
                "StBindingNativePinAccessor<"
                f"{enum_value}, {pin_id}U>"
            )
            if (pin.direction in {"input", "in_out"} and
                    pin.adapter != "constant"):
                native_enum = pin.native_type.split("::")[-1]
                if (pin.adapter == "enum_cast" and
                        binding_types.get(pin.st_type, {}).get("kind") != "enum" and
                        native_enum not in native_enums):
                    raise RuntimeError(
                        f"{fb.name}.{pin.name}: enum values unavailable for "
                        f"{pin.native_type}"
                    )
                store_mask |= 1 << pin_id
                store_scalar_count += 1
                fb_store_cases.append(f"    case {pin_id}U: {{")
                fb_store_cases.extend(
                    scalar_store_body(pin, accessor, binding_types,
                                      native_enums)
                )
                fb_store_cases.append("    }")
            if pin.direction in {"output", "in_out"}:
                load_mask |= 1 << pin_id
                load_scalar_count += 1
                fb_load_cases.append(f"    case {pin_id}U: {{")
                fb_load_cases.extend(
                    scalar_load_body(pin, accessor, binding_types)
                )
                fb_load_cases.append("    }")

        capability_cases.append(
            f"    case {enum_value}: return {{0x{store_mask:016x}ULL, "
            f"0x{load_mask:016x}ULL, "
            f"0x{store_object_mask:016x}ULL, "
            f"0x{load_object_mask:016x}ULL, "
            f"0x{store_sequence_mask:016x}ULL, "
            f"0x{load_sequence_mask:016x}ULL, "
            f"0x{store_tagged_reference_mask:016x}ULL, "
            f"0x{load_tagged_reference_mask:016x}ULL}};"
        )
        if fb_store_cases:
            store_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_store_cases,
                "        default: return false;",
                "        }",
            ])
        if fb_load_cases:
            load_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_load_cases,
                "        default: return false;",
                "        }",
            ])
        if fb_store_object_cases:
            store_object_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_store_object_cases,
                "        default: return false;",
                "        }",
            ])
        if fb_load_object_cases:
            load_object_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_load_object_cases,
                "        default: return false;",
                "        }",
            ])
        if fb_store_sequence_cases:
            store_sequence_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_store_sequence_cases,
                "        default: return false;",
                "        }",
            ])
        if fb_load_sequence_cases:
            load_sequence_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_load_sequence_cases,
                "        default: return false;",
                "        }",
            ])
        if fb_store_tagged_reference_cases:
            store_tagged_reference_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_store_tagged_reference_cases,
                "        default: return false;",
                "        }",
            ])
        if fb_load_tagged_reference_cases:
            load_tagged_reference_cases.extend([
                f"    case {enum_value}:",
                "        switch(pin) {",
                *fb_load_tagged_reference_cases,
                "        default: return false;",
                "        }",
            ])

    lines = [
        "#pragma once",
        "",
        "// Generated by tools/generate_st_binding_pin_catalog.py; do not edit.",
        "// Only pins present in the checked-in adapter overlay receive an",
        "// available accessor specialization. Runtime registration is",
        "// determined independently by generated dispatcher capabilities.",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <cstring>",
        "#include <limits>",
        "#include <new>",
        "#include <type_traits>",
        "#include <utility>",
        "",
        '#include "st/type_desc.h"',
        '#include "st/generated/st_binding_catalog.h"',
        *[f'#include "{header}"' for header in includes],
        "",
        "namespace plcopen::core::st::generated",
        "{",
        "",
        "constexpr std::size_t st_binding_native_size(",
        "    StBindingFbType type) noexcept",
        "{",
        "    switch(type) {",
        *size_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return 0U;",
        "}",
        "",
        "constexpr std::size_t st_binding_native_align(",
        "    StBindingFbType type) noexcept",
        "{",
        "    switch(type) {",
        *align_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return 0U;",
        "}",
        "",
        "inline bool st_binding_default_construct(",
        "    StBindingFbType type, void *storage)",
        "{",
        "    if(storage == nullptr) return false;",
        "    switch(type) {",
        *construct_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_destruct(",
        "    StBindingFbType type, void *storage) noexcept",
        "{",
        "    if(storage == nullptr) return false;",
        "    switch(type) {",
        *destruct_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_invoke(StBindingFbType type, void *storage,",
        "                              std::int64_t task_period = 0)",
        "{",
        "    if(storage == nullptr) return false;",
        "    switch(type) {",
        *invoke_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "enum class StBindingNativeCapability : std::uint8_t",
        "{",
        "    store_scalar = 1U << 0U,",
        "    load_scalar = 1U << 1U,",
        "    store_object = 1U << 2U,",
        "    load_object = 1U << 3U,",
        "    store_sequence = 1U << 4U,",
        "    load_sequence = 1U << 5U,",
        "    store_tagged_reference = 1U << 6U,",
        "    load_tagged_reference = 1U << 7U,",
        "};",
        "",
        "struct StBindingNativeCapabilityMasks",
        "{",
        "    std::uint64_t store_scalar = 0U;",
        "    std::uint64_t load_scalar = 0U;",
        "    std::uint64_t store_object = 0U;",
        "    std::uint64_t load_object = 0U;",
        "    std::uint64_t store_sequence = 0U;",
        "    std::uint64_t load_sequence = 0U;",
        "    std::uint64_t store_tagged_reference = 0U;",
        "    std::uint64_t load_tagged_reference = 0U;",
        "};",
        "",
        "constexpr StBindingNativeCapabilityMasks",
        "st_binding_native_capabilities(StBindingFbType type) noexcept",
        "{",
        "    switch(type) {",
        *capability_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return {};",
        "}",
        "",
        "constexpr bool st_binding_capability_has(std::uint64_t mask,",
        "                                         std::uint16_t pin) noexcept",
        "{",
        "    return pin < 64U && (mask & (std::uint64_t{1} << pin)) != 0U;",
        "}",
        "",
        "constexpr bool st_binding_can_store_scalar(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        "        st_binding_native_capabilities(type).store_scalar, pin);",
        "}",
        "",
        "constexpr bool st_binding_can_load_scalar(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        "        st_binding_native_capabilities(type).load_scalar, pin);",
        "}",
        "",
        "constexpr bool st_binding_can_store_object(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        "        st_binding_native_capabilities(type).store_object, pin);",
        "}",
        "",
        "constexpr bool st_binding_can_load_object(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        "        st_binding_native_capabilities(type).load_object, pin);",
        "}",
        "",
        "constexpr bool st_binding_can_store_sequence(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        "        st_binding_native_capabilities(type).store_sequence, pin);",
        "}",
        "",
        "constexpr bool st_binding_can_load_sequence(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        "        st_binding_native_capabilities(type).load_sequence, pin);",
        "}",
        "",
        "constexpr bool st_binding_can_store_tagged_reference(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        ("        st_binding_native_capabilities(type)."
         "store_tagged_reference, pin);"),
        "}",
        "",
        "constexpr bool st_binding_can_load_tagged_reference(",
        "    StBindingFbType type, std::uint16_t pin) noexcept",
        "{",
        "    return st_binding_capability_has(",
        ("        st_binding_native_capabilities(type)."
         "load_tagged_reference, pin);"),
        "}",
        "",
        "struct StBindingNativeSequenceValue",
        "{",
        "    const void *data = nullptr;",
        "    std::size_t count = 0U;",
        "    bool flag = false;",
        "};",
        "",
        "enum class StBindingNativeTaggedReferenceTag : std::uint8_t",
        "{",
        "    none = 0,",
        "    kinematics = 1,",
        "    pose = 2,",
        "};",
        "",
        "struct StBindingNativeTaggedReferenceValue",
        "{",
        "    const void *data = nullptr;",
        "    StBindingNativeTaggedReferenceTag tag =",
        "        StBindingNativeTaggedReferenceTag::none;",
        "};",
        "",
        "template <StBindingFbType Fb, std::uint16_t Pin>",
        "struct StBindingNativePinAccessor",
        "{",
        "    static constexpr bool available = false;",
        "    static constexpr bool registered = false;",
        "    static constexpr StBindingAdapterKind kind =",
        "        StBindingAdapterKind::unresolved;",
        "};",
        "",
        *accessor_rows,
        "inline double st_binding_double_from_bits(std::uint64_t bits) noexcept",
        "{",
        "    double value = 0.0;",
        "    std::memcpy(&value, &bits, sizeof(value));",
        "    return value;",
        "}",
        "",
        "inline std::uint64_t st_binding_double_bits(double value) noexcept",
        "{",
        "    std::uint64_t bits = 0U;",
        "    std::memcpy(&bits, &value, sizeof(bits));",
        "    return bits;",
        "}",
        "",
        "template <typename T>",
        "inline T st_binding_object_read(const unsigned char *bytes,",
        "                                std::size_t offset) noexcept",
        "{",
        "    T value{};",
        "    std::memcpy(&value, bytes + offset, sizeof(value));",
        "    return value;",
        "}",
        "",
        "template <typename T>",
        "inline void st_binding_object_write(unsigned char *bytes,",
        "                                    std::size_t offset,",
        "                                    T value) noexcept",
        "{",
        "    std::memcpy(bytes + offset, &value, sizeof(value));",
        "}",
        "",
        *render_object_codec_helpers(binding_types),
        "inline bool st_binding_store_scalar(StBindingFbType type,",
        "                                    std::uint16_t pin, void *storage,",
        "                                    std::uint64_t bits) noexcept",
        "{",
        "    if(storage == nullptr || !st_binding_can_store_scalar(type, pin))",
        "        return false;",
        "    switch(type) {",
        *store_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_load_scalar(StBindingFbType type,",
        "                                   std::uint16_t pin,",
        "                                   const void *storage,",
        "                                   std::uint64_t &bits) noexcept",
        "{",
        "    // binding_ref loads expose transient native pointer bits only.",
        "    // A VM boundary must reverse-resolve an existing ST handle before",
        "    // writing the result to stack or variable storage.",
        "    if(storage == nullptr || !st_binding_can_load_scalar(type, pin))",
        "        return false;",
        "    switch(type) {",
        *load_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_store_object(StBindingFbType type,",
        "                                    std::uint16_t pin, void *storage,",
        "                                    const unsigned char *bytes,",
        "                                    TypeId type_id,",
        "                                    std::uint32_t size) noexcept",
        "{",
        "    if(storage == nullptr || bytes == nullptr ||",
        "       !st_binding_can_store_object(type, pin)) return false;",
        "    switch(type) {",
        *store_object_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_load_object(StBindingFbType type,",
        "                                   std::uint16_t pin,",
        "                                   const void *storage,",
        "                                   unsigned char *bytes,",
        "                                   TypeId type_id,",
        "                                   std::uint32_t size) noexcept",
        "{",
        "    if(storage == nullptr || bytes == nullptr ||",
        "       !st_binding_can_load_object(type, pin)) return false;",
        "    switch(type) {",
        *load_object_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_store_sequence(",
        "    StBindingFbType type, std::uint16_t pin, void *storage,",
        "    StBindingNativeSequenceValue value) noexcept",
        "{",
        "    if(storage == nullptr ||",
        "       !st_binding_can_store_sequence(type, pin)) return false;",
        "    switch(type) {",
        *store_sequence_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_load_sequence(",
        "    StBindingFbType type, std::uint16_t pin, const void *storage,",
        "    StBindingNativeSequenceValue &value) noexcept",
        "{",
        "    // This extracts native registry keys only. The VM/load domain",
        "    // must reverse-resolve them before exposing an ST handle.",
        "    if(storage == nullptr ||",
        "       !st_binding_can_load_sequence(type, pin)) return false;",
        "    switch(type) {",
        *load_sequence_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_store_tagged_reference(",
        "    StBindingFbType type, std::uint16_t pin, void *storage,",
        "    TypeId type_id,",
        "    StBindingNativeTaggedReferenceValue value) noexcept",
        "{",
        "    if(storage == nullptr ||",
        ("       !st_binding_can_store_tagged_reference(type, pin)) "
         "return false;"),
        "    switch(type) {",
        *store_tagged_reference_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "inline bool st_binding_load_tagged_reference(",
        "    StBindingFbType type, std::uint16_t pin, const void *storage,",
        "    TypeId type_id,",
        "    StBindingNativeTaggedReferenceValue &value) noexcept",
        "{",
        "    if(storage == nullptr ||",
        ("       !st_binding_can_load_tagged_reference(type, pin)) "
         "return false;"),
        "    switch(type) {",
        *load_tagged_reference_cases,
        "    case StBindingFbType::count: break;",
        "    }",
        "    return false;",
        "}",
        "",
        "constexpr bool st_binding_native_capabilities_valid() noexcept",
        "{",
        "    for(const StBindingFbMetadata &fb : kStBindingFbs) {",
        "        const StBindingNativeCapabilityMasks masks =",
        "            st_binding_native_capabilities(fb.type);",
        "        const std::uint64_t valid =",
        "            fb.pin_count == 0U ? 0U :",
        "            ((std::uint64_t{1} << fb.pin_count) - 1U);",
        "        if(((masks.store_scalar | masks.load_scalar |",
        "             masks.store_object | masks.load_object |",
        "             masks.store_sequence | masks.load_sequence |",
        ("             masks.store_tagged_reference | "
         "masks.load_tagged_reference) & ~valid) != 0U)"),
        "            return false;",
        "        for(std::uint16_t pin = 0U; pin < fb.pin_count; ++pin) {",
        "            const StBindingPinMetadata &metadata =",
        "                kStBindingPins[fb.first_pin + pin];",
        "            if(st_binding_capability_has(masks.store_scalar, pin) &&",
        "               metadata.direction != StBindingPinDirection::input &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        "            if(st_binding_capability_has(masks.load_scalar, pin) &&",
        "               metadata.direction != StBindingPinDirection::output &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        "            if(st_binding_capability_has(masks.store_object, pin) &&",
        "               metadata.direction != StBindingPinDirection::input &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        "            if(st_binding_capability_has(masks.load_object, pin) &&",
        "               metadata.direction != StBindingPinDirection::output &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        "            if(st_binding_capability_has(masks.store_sequence, pin) &&",
        "               metadata.direction != StBindingPinDirection::input &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        "            if(st_binding_capability_has(masks.load_sequence, pin) &&",
        "               metadata.direction != StBindingPinDirection::output &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        ("            if(st_binding_capability_has("
         "masks.store_tagged_reference, pin) &&"),
        "               metadata.direction != StBindingPinDirection::input &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        ("            if(st_binding_capability_has("
         "masks.load_tagged_reference, pin) &&"),
        "               metadata.direction != StBindingPinDirection::output &&",
        "               metadata.direction != StBindingPinDirection::in_out)",
        "                return false;",
        "            if((st_binding_capability_has(masks.store_scalar, pin) ||",
        "                st_binding_capability_has(masks.load_scalar, pin)) &&",
        "               (metadata.st_type == \"UNRESOLVED\" ||",
        "                metadata.adapter == StBindingAdapterKind::unresolved ||",
        "                metadata.adapter == StBindingAdapterKind::sequence))",
        "                return false;",
        ("            if((st_binding_capability_has("
         "masks.store_tagged_reference, pin) ||"),
        ("                st_binding_capability_has("
         "masks.load_tagged_reference, pin)) &&"),
        "               (metadata.st_type == \"UNRESOLVED\" ||",
        ("                metadata.adapter != "
         "StBindingAdapterKind::tagged_reference))"),
        "                return false;",
        "            if((st_binding_capability_has(masks.store_object, pin) ||",
        "                st_binding_capability_has(masks.load_object, pin)) &&",
        "               (metadata.st_type == \"UNRESOLVED\" ||",
        "                metadata.adapter != StBindingAdapterKind::direct))",
        "                return false;",
        "            if((st_binding_capability_has(masks.store_sequence, pin) ||",
        "                st_binding_capability_has(masks.load_sequence, pin)) &&",
        "               (metadata.st_type == \"UNRESOLVED\" ||",
        "                (metadata.adapter != StBindingAdapterKind::direct &&",
        "                 metadata.adapter != StBindingAdapterKind::sequence)))",
        "                return false;",
        "            const std::uint64_t scalar =",
        "                masks.store_scalar | masks.load_scalar;",
        "            const std::uint64_t object =",
        "                masks.store_object | masks.load_object;",
        "            const std::uint64_t sequence =",
        "                masks.store_sequence | masks.load_sequence;",
        "            const std::uint64_t tagged_reference =",
        ("                masks.store_tagged_reference | "
         "masks.load_tagged_reference;"),
        "            if((scalar & object) != 0U || (scalar & sequence) != 0U ||",
        "               (scalar & tagged_reference) != 0U ||",
        "               (object & sequence) != 0U ||",
        "               (object & tagged_reference) != 0U ||",
        "               (sequence & tagged_reference) != 0U)",
        "                return false;",
        "        }",
        "    }",
        "    return true;",
        "}",
        "",
        f"inline constexpr std::size_t kStBindingStoreScalarPinCount = {store_scalar_count}U;",
        f"inline constexpr std::size_t kStBindingLoadScalarPinCount = {load_scalar_count}U;",
        f"inline constexpr std::size_t kStBindingStoreObjectPinCount = {store_object_count}U;",
        f"inline constexpr std::size_t kStBindingLoadObjectPinCount = {load_object_count}U;",
        f"inline constexpr std::size_t kStBindingStoreSequencePinCount = {store_sequence_count}U;",
        f"inline constexpr std::size_t kStBindingLoadSequencePinCount = {load_sequence_count}U;",
        ("inline constexpr std::size_t "
         "kStBindingStoreTaggedReferencePinCount = "
         f"{store_tagged_reference_count}U;"),
        ("inline constexpr std::size_t "
         "kStBindingLoadTaggedReferencePinCount = "
         f"{load_tagged_reference_count}U;"),
        "static_assert(st_binding_native_capabilities_valid());",
        *construct_assertions,
        "",
        "} // namespace plcopen::core::st::generated",
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--allow-unresolved", action="store_true")
    args = parser.parse_args()
    fbs = build()
    rendered, counts = render(fbs)
    gaps = render_gaps(fbs)
    cpp = render_cpp(fbs)
    pin_desc_cpp = render_pin_desc_cpp(fbs)
    native_cpp = render_native_cpp(fbs)
    if args.update:
        PIN_CATALOG.parent.mkdir(parents=True, exist_ok=True)
        CPP_CATALOG.parent.mkdir(parents=True, exist_ok=True)
        PIN_CATALOG.write_text(rendered, encoding="utf-8", newline="\n")
        GAP_CATALOG.write_text(gaps, encoding="utf-8", newline="\n")
        CPP_CATALOG.write_text(cpp, encoding="utf-8", newline="\n")
        PIN_DESC_CATALOG.write_text(
            pin_desc_cpp, encoding="utf-8", newline="\n"
        )
        NATIVE_CATALOG.write_text(native_cpp, encoding="utf-8", newline="\n")
    elif not PIN_CATALOG.exists():
        print(f"missing generated pin catalog: {PIN_CATALOG}", file=sys.stderr)
        return 1
    elif PIN_CATALOG.read_text(encoding="utf-8") != rendered:
        print("generated pin catalog is stale; run with --update", file=sys.stderr)
        return 1
    elif not GAP_CATALOG.exists():
        print(f"missing generated native gap catalog: {GAP_CATALOG}",
              file=sys.stderr)
        return 1
    elif GAP_CATALOG.read_text(encoding="utf-8") != gaps:
        print("generated native gap catalog is stale; run with --update",
              file=sys.stderr)
        return 1
    elif not CPP_CATALOG.exists():
        print(f"missing generated C++ binding metadata: {CPP_CATALOG}",
              file=sys.stderr)
        return 1
    elif CPP_CATALOG.read_text(encoding="utf-8") != cpp:
        print("generated C++ binding metadata is stale; run with --update",
              file=sys.stderr)
        return 1
    elif not PIN_DESC_CATALOG.exists():
        print(f"missing generated C++ pin tables: {PIN_DESC_CATALOG}",
              file=sys.stderr)
        return 1
    elif PIN_DESC_CATALOG.read_text(encoding="utf-8") != pin_desc_cpp:
        print("generated C++ pin tables are stale; run with --update",
              file=sys.stderr)
        return 1
    elif not NATIVE_CATALOG.exists():
        print(f"missing generated C++ native adapters: {NATIVE_CATALOG}",
              file=sys.stderr)
        return 1
    elif NATIVE_CATALOG.read_text(encoding="utf-8") != native_cpp:
        print("generated C++ native adapters are stale; run with --update",
              file=sys.stderr)
        return 1
    print("ST binding pins: " + ", ".join(
        f"{key}={value}" for key, value in counts.items()
    ))
    unresolved = [
        f"{fb.name}.{pin.name}:{pin.resolution}"
        for fb in fbs for pin in fb.pins if pin.resolution != "mapped"
    ]
    unresolved.extend(
        f"{fb.name}:{fb.resolution}" for fb in fbs if fb.resolution != "mapped"
    )
    if unresolved:
        for item in unresolved:
            print(f"UNRESOLVED {item}")
        if not args.allow_unresolved:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

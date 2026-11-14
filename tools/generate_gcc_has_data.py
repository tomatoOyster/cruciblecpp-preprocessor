#!/usr/bin/env python3
"""Generate complete GCC 15.2 __has_* tables from GCC source + the compiler.

Usage:
  python tools/generate_gcc_has_data.py \
    --gcc-source /path/to/gcc-15.2.0 \
    --gcc g++ \
    --output include/GccHasData.inc

The generated data is intentionally checked in. Regenerate it when changing
the supported GCC version.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

STANDARDS = ("c++17", "c++20", "c++23")
CXX_KEYWORDS = set(
    """
    alignas alignof and and_eq asm auto bitand bitor bool break case catch
    char char8_t char16_t char32_t class compl concept const consteval
    constexpr constinit const_cast continue co_await co_return co_yield
    decltype default delete do double dynamic_cast else enum explicit export
    extern false float for friend goto if inline int long mutable namespace
    new noexcept not not_eq nullptr operator or or_eq private protected public
    register reinterpret_cast requires return short signed sizeof static
    static_assert static_cast struct switch template this thread_local throw
    true try typedef typeid typename union unsigned using virtual void volatile
    wchar_t while xor xor_eq
    """.split()
)
FLOAT_SUFFIXES = ("f16", "f32", "f64", "f128", "f32x", "f64x", "f128x")


def read_text(path: str) -> str:
    with open(path, encoding="utf-8", errors="ignore") as handle:
        return handle.read()


def valid_identifier(name: str) -> bool:
    return bool(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name)) and name not in CXX_KEYWORDS


def collect_builtins(source: str) -> set[str]:
    builtins: set[str] = set()
    builtins_def = read_text(os.path.join(source, "gcc", "builtins.def"))
    for line in builtins_def.splitlines():
        match = re.match(
            r'^\s*(DEF_[A-Z0-9_]+)\s*\(\s*[^,]+,\s*"([^"]+)"', line
        )
        if not match:
            continue
        macro, name = match.group(1), match.group(2)
        if "FLOATN_NX" in macro:
            for suffix in FLOAT_SUFFIXES:
                builtins.add("__builtin_" + name + suffix)
        elif macro == "DEF_SYNC_BUILTIN":
            builtins.add(name)
        elif macro == "DEF_BUILTIN":
            builtins.add(name)
        else:
            builtins.add("__builtin_" + name)

    traits = read_text(os.path.join(source, "gcc", "cp", "cp-trait.def"))
    for match in re.finditer(
        r'DEFTRAIT(?:_EXPR|_TYPE)?\s*\([^,]+,\s*"([^"]+)"', traits
    ):
        builtins.add(match.group(1).strip())

    # GCC source contains aliases and target builtins outside builtins.def.
    for root, _dirs, files in os.walk(os.path.join(source, "gcc")):
        for filename in files:
            if not filename.endswith((".cc", ".h", ".def")):
                continue
            text = read_text(os.path.join(root, filename))
            for match in re.finditer(
                r'"(__builtin_[A-Za-z0-9_]+|__is_[A-Za-z0-9_]+)"', text
            ):
                builtins.add(match.group(1))
            # Some front-end builtins (for example
            # __builtin_operator_new) are referenced as identifiers rather
            # than as quoted builtins.def names.
            for match in re.finditer(
                r"\b(__builtin_[A-Za-z0-9_]+|__is_[A-Za-z0-9_]+)\b", text
            ):
                builtins.add(match.group(1))
    builtins.add("__is_same_as")
    # Front-end-only allocation builtins used by libstdc++.
    builtins.add("__builtin_operator_new")
    builtins.add("__builtin_operator_delete")
    return {name for name in builtins if valid_identifier(name)}


def collect_attributes(source: str) -> set[str]:
    attributes: set[str] = set()
    for root, _dirs, files in os.walk(os.path.join(source, "gcc")):
        for filename in files:
            if not filename.endswith((".cc", ".h", ".def")):
                continue
            text = read_text(os.path.join(root, filename))
            if not (
                "attribute_spec" in text
                or "ATTR_EXCL" in text
                or "attribute_table" in text
                or "c_common_gnu_attributes" in text
            ):
                continue
            for match in re.finditer(
                r'"([A-Za-z_][A-Za-z0-9_]*)"', text
            ):
                name = match.group(1)
                if valid_identifier(name):
                    attributes.add(name)
    return attributes


def collect_features(source: str) -> set[str]:
    features: set[str] = set()
    files = (
        ("gcc", "c-family", "c-common.cc"),
        ("gcc", "cp", "cp-objcp-common.cc"),
        ("gcc", "c", "c-objc-common.cc"),
    )
    for parts in files:
        text = read_text(os.path.join(source, *parts))
        for match in re.finditer(r'"([a-z][a-z0-9_]*)"', text):
            features.add(match.group(1))
    return features


def run_query(gcc: str, standard: str, lines: list[str]) -> str:
    process = subprocess.run(
        [gcc, "-std=" + standard, "-E", "-P", "-x", "c++", "-"],
        input="\n".join(lines) + "\n",
        text=True,
        capture_output=True,
    )
    if process.returncode != 0:
        sys.stderr.write(process.stderr[:4000])
        raise RuntimeError("GCC query failed for " + standard)
    return process.stdout


def query_builtin_values(
    gcc: str, names: set[str]
) -> dict[str, int]:
    ordered = sorted(names)
    lines = [
        f"BUILTIN_VALUE {index} __has_builtin({name})"
        for index, name in enumerate(ordered)
    ]
    output = run_query(gcc, "c++20", lines)
    values: dict[str, int] = {}
    for line in output.splitlines():
        match = re.match(r"BUILTIN_VALUE\s+(\d+)\s+(\d+)", line)
        if not match:
            continue
        value = int(match.group(2))
        if value:
            values[ordered[int(match.group(1))]] = value
    return values


def query_members(
    gcc: str, kind: str, names: set[str], standard: str = "c++20"
) -> set[str]:
    ordered = sorted(names)
    lines: list[str] = []
    for index, name in enumerate(ordered):
        if kind == "cpp":
            lines.append(f"ATTR {index} __has_cpp_attribute({name})")
        else:
            lines.extend(
                [
                    f"#if __has_{kind}({name})",
                    f"{kind.upper()} {index}",
                    "#endif",
                ]
            )
    output = run_query(gcc, standard, lines)
    found: set[str] = set()
    if kind == "cpp":
        for line in output.splitlines():
            match = re.match(r"ATTR\s+(\d+)\s+(\d+)", line)
            if match and int(match.group(2)) != 0:
                found.add(ordered[int(match.group(1))])
    else:
        prefix = kind.upper() + " "
        for line in output.splitlines():
            if line.startswith(prefix):
                found.add(ordered[int(line[len(prefix):])])
    return found


def query_cpp_attributes(
    gcc: str, names: set[str], standard: str
) -> dict[str, int]:
    ordered = sorted(names)
    lines: list[str] = [
        f"ATTR {index} __has_cpp_attribute({name})"
        for index, name in enumerate(ordered)
    ]
    output = run_query(gcc, standard, lines)
    found: dict[str, int] = {}
    for line in output.splitlines():
        match = re.match(r"ATTR\s+(\d+)\s+(\d+)", line)
        if match:
            value = int(match.group(2))
            if value:
                found[ordered[int(match.group(1))]] = value
    return found


def cpp_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_array(handle, name: str, values: list[str]) -> None:
    handle.write(f"inline constexpr std::string_view {name}[] = {{\n")
    for value in values:
        handle.write(f"    {cpp_string(value)},\n")
    handle.write("};\n\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc-source", required=True)
    parser.add_argument("--gcc", default="g++")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    version = subprocess.run(
        [args.gcc, "-dumpfullversion"], text=True, capture_output=True
    ).stdout.strip()
    if not version.startswith("15.2"):
        raise RuntimeError("expected GCC 15.2, got " + version)

    builtins = collect_builtins(args.gcc_source)
    attributes = collect_attributes(args.gcc_source)
    features = collect_features(args.gcc_source)
    cpp_candidates = set(attributes)
    cpp_candidates.update(
        {
            "deprecated",
            "fallthrough",
            "maybe_unused",
            "nodiscard",
            "noreturn",
            "no_unique_address",
            "likely",
            "unlikely",
            "assume",
            "carries_dependency",
        }
    )
    cpp_candidates.update({"gnu::" + name for name in attributes})
    cpp_candidates.update({"clang::flag_enum", "clang::musttail"})

    true_builtins = sorted(query_members(args.gcc, "builtin", builtins))
    builtin_values = query_builtin_values(args.gcc, set(true_builtins))
    true_attributes = sorted(query_members(args.gcc, "attribute", attributes))
    true_features = {
        standard: query_members(args.gcc, "feature", features, standard)
        for standard in STANDARDS
    }
    true_extensions = {
        standard: query_members(args.gcc, "extension", features, standard)
        for standard in STANDARDS
    }
    cpp_attributes = {
        standard: query_cpp_attributes(args.gcc, cpp_candidates, standard)
        for standard in STANDARDS
    }

    all_features = sorted(set().union(*true_features.values(), *true_extensions.values()))
    all_cpp_attributes = sorted(
        set().union(*(values.keys() for values in cpp_attributes.values()))
    )

    output = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("// Generated from GCC 15.2 source and compiler. Do not edit.\n")
        handle.write(f"// GCC version: {version}\n")
        handle.write("namespace crucible::gcc_data {\n\n")
        write_array(handle, "kBuiltins", true_builtins)
        handle.write(
            "struct BuiltinValue { std::string_view name; int value; };\n"
        )
        handle.write("inline constexpr BuiltinValue kBuiltinValues[] = {\n")
        for name in sorted(builtin_values):
            if builtin_values[name] != 1:
                handle.write(
                    f"    {{{cpp_string(name)}, {builtin_values[name]}}},\n"
                )
        handle.write("};\n\n")
        write_array(handle, "kAttributes", true_attributes)
        handle.write(
            "struct FeatureSupport { std::string_view name; unsigned features; "
            "unsigned extensions; };\n"
        )
        handle.write("inline constexpr FeatureSupport kFeatures[] = {\n")
        for name in all_features:
            feature_mask = sum(
                1 << index
                for index, standard in enumerate(STANDARDS)
                if name in true_features[standard]
            )
            extension_mask = sum(
                1 << index
                for index, standard in enumerate(STANDARDS)
                if name in true_extensions[standard]
            )
            handle.write(
                f"    {{{cpp_string(name)}, {feature_mask}u, {extension_mask}u}},\n"
            )
        handle.write("};\n\n")
        handle.write(
            "struct CppAttributeSupport { std::string_view name; int cxx17; "
            "int cxx20; int cxx23; };\n"
        )
        handle.write("inline constexpr CppAttributeSupport kCppAttributes[] = {\n")
        for name in all_cpp_attributes:
            values = [
                cpp_attributes[standard].get(name, 0)
                for standard in STANDARDS
            ]
            handle.write(
                f"    {{{cpp_string(name)}, {values[0]}, {values[1]}, {values[2]}}},\n"
            )
        handle.write("};\n\n")
        handle.write("} // namespace crucible::gcc_data\n")

    print(
        f"wrote {output}: builtins={len(true_builtins)} "
        f"builtin_values={len(builtin_values)} "
        f"attributes={len(true_attributes)} features={len(all_features)} "
        f"cpp_attributes={len(all_cpp_attributes)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Run a subset of GCC's g++.dg/cpp tests against CrucibleCpp.

GCC tests contain DejaGnu directives. This runner extracts dg-options and
dg-error/dg-warning, then delegates the actual comparison to the differential
harness. Tests using unsupported language modes or preprocessor options are
reported as skipped rather than silently counted as passes.

Usage:
  python tests/gcc_testsuite/run_gcc_tests.py \
    --gcc g++ --preprocessor build/cruciblecpp-preprocessor.exe \
    --testsuite C:/gcc-15.2.0/gcc/testsuite/g++.dg/cpp
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DIFFERENTIAL = HERE.parent / "differential" / "run_differential.py"
spec = importlib.util.spec_from_file_location("differential", DIFFERENTIAL)
differential = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(differential)

SKIP_NAMES = {
    "delimited-escape-seq-1.C", "delimited-escape-seq-2.C",
    "c++98.C", "c++98-pedantic.C", "pr23827_cxx98.C",
    "pr23827_cxx98_neg.C", "pr23827_cxx11.C",
    "pr80005.C", "pragma-float-const-decimal64-1.C",
    "elifdef-3.C", "elifdef-5.C", "elifdef-6.C",
}
SKIP_OPTIONS = (
    "-fdirectives-only", "-traditional-cpp", "-fpreprocessed",
    "-Winvalid-pch", "-fno-", "-fmodules", "-fmodule",
    "-pedantic-errors",
)
IGNORED_OPTIONS = {"-ansi", "-c", "-save-temps", "-o"}
SUPPORTED_PREFIXES = (
    "-std=", "-D", "-U", "-I", "-include", "-isystem", "-W",
    "-pedantic", "-trigraphs", "-finput-charset", "-fexec-charset",
    "--embed-dir",
)


def parse_options(text: str, testsuite: Path) -> list[str]:
    options: list[str] = []
    for match in re.finditer(r"dg-options\s+\"([^\"]*)\"", text):
        raw = match.group(1).replace("\\\\", "\\")
        # ${srcdir} in GCC tests refers to gcc/testsuite, not the cpp
        # subdirectory passed on the command line.
        source_root = testsuite.parents[1] if len(testsuite.parents) > 1 else testsuite
        raw = raw.replace("${srcdir}", str(source_root))
        options.extend(raw.split())
    return options


def supported(options: list[str]) -> str | None:
    for option in options:
        if option in IGNORED_OPTIONS:
            continue
        if any(bad in option for bad in SKIP_OPTIONS):
            return "unsupported option " + option
        if option.startswith("-std="):
            standard = option[5:].replace("gnu", "c++", 1)
            if standard.startswith("c++"):
                standard = standard[3:]
            if standard not in ("17", "20", "23", "2a", "2b"):
                return "unsupported standard " + option
            continue
        if not option.startswith(SUPPORTED_PREFIXES):
            return "unsupported option " + option
    return None


def normalize_standard(options: list[str]) -> list[str]:
    result = []
    for option in options:
        if option in IGNORED_OPTIONS:
            continue
        if option.startswith("-std="):
            standard = option[5:]
            standard = standard.replace("gnu++", "c++")
            if standard == "c++2a":
                standard = "c++20"
            elif standard == "c++2b":
                standard = "c++23"
            elif standard in ("c++11", "c++14"):
                standard = "c++17"
            elif standard in ("c++98", "c++03"):
                return []
            option = "-std=" + standard
        result.append(option)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc", required=True)
    parser.add_argument("--preprocessor", required=True)
    parser.add_argument("--testsuite", required=True)
    parser.add_argument("--filter", default="")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--report")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()

    testsuite = Path(args.testsuite).resolve()
    tests = sorted(testsuite.glob("*.C"))
    if args.filter:
        tests = [test for test in tests if args.filter in test.name]
    if args.limit:
        tests = tests[:args.limit]

    results = []
    for test in tests:
        text = test.read_text(encoding="utf-8", errors="replace")
        reason = None
        if test.name in SKIP_NAMES:
            reason = "unsupported language feature"
        elif "../../gcc.dg/cpp/" in text:
            reason = "C fixture with independent dg-options"
        options = parse_options(text, testsuite)
        if reason is None:
            reason = supported(options)
        options = normalize_standard(options)
        if reason is None and not options and "-std=" in text:
            reason = "unsupported standard"
        if reason is not None:
            results.append({"name": test.name, "status": "skip",
                            "reason": reason})
            print("SKIP  %-35s %s" % (test.name, reason))
            continue

        case = {
            "name": test.stem,
            "file": test.name,
            "args": options,
            "expect_error": "dg-error" in text,
            "std": "c++20" if any(option.startswith("-std=") for option in options)
            else "c++20",
        }
        try:
            result = differential.run_case(
                case, testsuite, args.gcc, args.preprocessor,
                args.timeout, "line",
            )
        except Exception as exc:
            result = {"name": case["name"], "ok": False,
                      "problems": ["harness error: %r" % (exc,)]}
        status = "pass" if result["ok"] else "fail"
        results.append({"name": test.name, "status": status,
                        "problems": result.get("problems", [])})
        print("%-5s %-35s" % (status.upper(), test.name))
        for problem in result.get("problems", []):
            print("      " + problem.replace("\n", "\n      "))

    summary = {
        "total": len(results),
        "passed": sum(1 for item in results if item["status"] == "pass"),
        "failed": sum(1 for item in results if item["status"] == "fail"),
        "skipped": sum(1 for item in results if item["status"] == "skip"),
        "results": results,
    }
    if args.report:
        Path(args.report).write_text(json.dumps(summary, indent=2),
                                     encoding="utf-8")
    print("\n%d passed, %d failed, %d skipped"
          % (summary["passed"], summary["failed"], summary["skipped"]))
    return 0 if summary["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

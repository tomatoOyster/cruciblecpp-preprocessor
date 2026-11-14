#!/usr/bin/env python3
"""Differential test runner: CrucibleCpp vs GCC -E.

For each corpus case this runner checks:
  * exit status (zero vs non-zero)
  * diagnostic severity and source line
  * preprocessed token stream
  * whether the preprocessed output is accepted by GCC

The comparison is intentionally token based. Whitespace and formatting are
not part of C++ preprocessing semantics; token spelling is.
"""

from __future__ import annotations

import argparse
import base64
import difflib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

PUNCTUATORS = [
    "%:%:", ">>=", "<<=", "->*", "...", "<=>", "##", "->", "++", "--",
    "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "*=", "/=", "%=",
    "+=", "-=", "&=", "|=", "^=", "::", ".*", "<:", ":>", "<%", "%>",
    "%:",
]
SINGLE = set("{}[]()#;:?.,~!+-*/%^&|<>=")
DIAGNOSTIC = re.compile(
    r"^(.*?):(\d+):(?:(\d+):)?\s*"
    r"(fatal error|error|warning|note):\s*(.*)$"
)


def decode_text(data: bytes) -> str:
    return data.decode("utf-8", errors="replace")


def run(command: list[str], cwd: Path, timeout: int) -> subprocess.CompletedProcess:
    return subprocess.run(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )


def strip_line_markers(text: str) -> str:
    result = []
    for line in text.splitlines():
        if re.match(r"^#\s+\d+\s+", line):
            continue
        result.append(line)
    return "\n".join(result) + "\n"


def expand_gnu_base64_embeds(text: str) -> str:
    # GCC -E may preserve large embeds as:
    #   #embed "." __gnu__::__base64__("...")
    # Canonicalize that representation back to bytes for token comparison.
    pattern = re.compile(
        r'#\s*embed\s+"[^"]*"\s+__gnu__::__base64__\s*\((.*?)\)',
        re.DOTALL,
    )

    def replace(match: re.Match) -> str:
        payload = "".join(re.findall(r'"([^"]*)"', match.group(1)))
        payload = payload.replace("\\\n", "").replace("\n", "")
        try:
            data = base64.b64decode(payload)
        except Exception:
            return match.group(0)
        return ", ".join(str(byte) for byte in data)

    return pattern.sub(replace, text)


def strip_compiler_diagnostic_pragmas(text: str) -> str:
    # GCC -E emits #pragma GCC diagnostic directives for the compiler. They are
    # diagnostic state, not part of the program semantics checked here.
    result = []
    for line in text.splitlines():
        if re.match(r"^\s*#\s*pragma\s+GCC\s+diagnostic\b", line):
            continue
        result.append(line)
    return "\n".join(result) + "\n"


def tokenize(text: str) -> list[str]:
    tokens: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if text.startswith("//", i):
            end = text.find("\n", i + 2)
            i = n if end < 0 else end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue

        prefix = ""
        for candidate in ("u8R\"", "uR\"", "UR\"", "LR\"", "R\"",
                          "u8\"", "u\"", "U\"", "L\""):
            if text.startswith(candidate, i):
                prefix = candidate
                break
        if prefix:
            start = i
            raw = prefix.endswith("R\"")
            if raw:
                open_paren = text.find("(", i + len(prefix))
                if open_paren < 0:
                    tokens.append(text[start:])
                    break
                delimiter = text[i + len(prefix):open_paren]
                closing = ")" + delimiter + "\""
                end = text.find(closing, open_paren + 1)
                if end < 0:
                    tokens.append(text[start:])
                    break
                i = end + len(closing)
                tokens.append(text[start:i])
                continue
            i += len(prefix) - 1
            quote = text[i]
        elif c in ("'", '"'):
            quote = c
        else:
            quote = ""

        if quote:
            start = i
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            tokens.append(text[start:i])
            continue

        if c.isalpha() or c == "_" or c == "$" or ord(c) >= 0x80:
            start = i
            i += 1
            while i < n:
                ch = text[i]
                if ch.isalnum() or ch in "_$" or ord(ch) >= 0x80:
                    i += 1
                else:
                    break
            tokens.append(text[start:i])
            continue

        if c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            start = i
            i += 1
            while i < n:
                ch = text[i]
                if ch.isalnum() or ch in "._'":
                    i += 1
                elif ch in "+-" and i > start and text[i - 1] in "eEpP":
                    i += 1
                else:
                    break
            tokens.append(text[start:i])
            continue

        matched = None
        for punct in PUNCTUATORS:
            if text.startswith(punct, i):
                matched = punct
                break
        if matched:
            tokens.append(matched)
            i += len(matched)
            continue
        if c in SINGLE:
            tokens.append(c)
            i += 1
            continue

        tokens.append(c)
        i += 1
    return tokens


def parse_diagnostics(stderr: str) -> list[tuple[str, int, str]]:
    result = []
    for line in stderr.splitlines():
        match = DIAGNOSTIC.match(line)
        if not match:
            continue
        path, line_number, _column, severity, message = match.groups()
        result.append(
            (Path(path).name.lower(), int(line_number), severity + ": " + message)
        )
    return result


def diagnostic_key(item, mode: str):
    path, line, message = item
    if mode == "none":
        return None
    if mode == "line":
        return (path, line, message.split(":", 1)[0])
    return item


def compare_tokens(expected: list[str], actual: list[str]) -> str | None:
    if expected == actual:
        return None
    diff = list(
        difflib.unified_diff(
            expected, actual, fromfile="gcc", tofile="cruciblecpp", n=8
        )
    )
    return "\n".join(diff[:80])


def load_cases(corpus: Path) -> list[dict]:
    manifest = corpus / "cases.json"
    with manifest.open(encoding="utf-8") as handle:
        cases = json.load(handle)
    if not isinstance(cases, list):
        raise ValueError("cases.json must contain a list")
    return cases


def run_case(case: dict, corpus: Path, gcc: str, preprocessor: str,
             timeout: int, diag_mode: str) -> dict:
    name = case["name"]
    source = case.get("file", name + ".cpp")
    std = case.get("std", "c++20")
    args = list(case.get("args", []))
    expect_error = bool(case.get("expect_error", False))

    with tempfile.TemporaryDirectory(prefix="crucible-diff-") as temp_text:
        temp = Path(temp_text)
        gcc_output = temp / (name + "-gcc.i")
        our_output = temp / (name + "-crucible.i")

        gcc_command = [
            gcc, "-std=" + std, "-E", "-P", "-fdiagnostics-color=never",
            *args, source,
        ]
        our_command = [
            preprocessor, "-P", "-std=" + std, "-fno-diagnostics-color",
            *args, source, "-o", str(our_output),
        ]

        gcc_result = run(gcc_command, corpus, timeout)
        our_result = run(our_command, corpus, timeout)
        gcc_text = expand_gnu_base64_embeds(
            strip_compiler_diagnostic_pragmas(
                strip_line_markers(decode_text(gcc_result.stdout))
            )
        )
        gcc_output.write_text(gcc_text, encoding="utf-8")
        our_text = strip_compiler_diagnostic_pragmas(
            strip_line_markers(
                our_output.read_text(encoding="utf-8", errors="replace")
                if our_output.exists() else ""
            )
        )

        problems = []
        gcc_failed = gcc_result.returncode != 0
        our_failed = our_result.returncode != 0
        if gcc_failed != our_failed:
            problems.append(
                "exit status differs: gcc=%d crucible=%d"
                % (gcc_result.returncode, our_result.returncode)
            )

        gcc_diag = parse_diagnostics(decode_text(gcc_result.stderr))
        our_diag = parse_diagnostics(decode_text(our_result.stderr))
        gcc_keys = [k for k in (diagnostic_key(d, diag_mode) for d in gcc_diag)
                    if k is not None]
        our_keys = [k for k in (diagnostic_key(d, diag_mode) for d in our_diag)
                    if k is not None]
        if gcc_keys != our_keys:
            problems.append(
                "diagnostics differ:\n  gcc: %s\n  crucible: %s"
                % (gcc_keys, our_keys)
            )

        token_difference = compare_tokens(tokenize(gcc_text),
                                          tokenize(our_text))
        if token_difference is not None:
            problems.append("token stream differs:\n" + token_difference)

        compile_ok = None
        if not expect_error and not gcc_failed and not our_failed:
            gcc_compile = run(
                [gcc, "-std=" + std, "-fsyntax-only", str(gcc_output)],
                temp, timeout,
            ) if gcc_output.exists() else None
            our_compile = run(
                [gcc, "-std=" + std, "-fsyntax-only", str(our_output)],
                temp, timeout,
            )
            gcc_ok = gcc_compile is not None and gcc_compile.returncode == 0
            compile_ok = our_compile.returncode == 0
            if gcc_ok and not compile_ok:
                problems.append(
                    "GCC accepted its output but rejected CrucibleCpp output:\n"
                    + decode_text(our_compile.stderr)[:4000]
                )

        return {
            "name": name,
            "ok": not problems,
            "problems": problems,
            "gcc_exit": gcc_result.returncode,
            "crucible_exit": our_result.returncode,
            "compiled": compile_ok,
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc", required=True)
    parser.add_argument("--preprocessor", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--diag-mode", choices=("none", "line", "full"),
                        default="line")
    parser.add_argument("--filter", default="")
    parser.add_argument("--report")
    args = parser.parse_args()

    corpus = Path(args.corpus).resolve()
    cases = load_cases(corpus)
    if args.filter:
        cases = [case for case in cases if args.filter in case["name"]]

    results = []
    for case in cases:
        try:
            result = run_case(case, corpus, args.gcc, args.preprocessor,
                              args.timeout, args.diag_mode)
        except Exception as exc:  # Harness failures are test failures.
            result = {
                "name": case.get("name", "?"),
                "ok": False,
                "problems": ["harness error: %r" % (exc,)],
                "gcc_exit": None,
                "crucible_exit": None,
                "compiled": None,
            }
        results.append(result)
        status = "PASS" if result["ok"] else "FAIL"
        print("%-5s %s" % (status, result["name"]))
        for problem in result["problems"]:
            print("      " + problem.replace("\n", "\n      "))

    report = {
        "total": len(results),
        "passed": sum(1 for result in results if result["ok"]),
        "failed": sum(1 for result in results if not result["ok"]),
        "results": results,
    }
    if args.report:
        Path(args.report).write_text(
            json.dumps(report, indent=2), encoding="utf-8"
        )
    print("\n%d/%d differential cases passed"
          % (report["passed"], report["total"]))
    return 0 if report["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

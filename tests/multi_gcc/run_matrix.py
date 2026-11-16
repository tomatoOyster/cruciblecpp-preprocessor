#!/usr/bin/env python3
"""Small three-compiler smoke matrix: preprocess, compile to exe, run."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
CASES = [
    ("basic", "tests/test_simple.cpp", []),
    ("functions", "tests/test_function_macro.cpp", []),
    ("conditional", "tests/test_conditional.cpp", []),
    ("pound", "tests/test_pound_ops.cpp", []),
    ("characters", "tests/test_character_constants.cpp", []),
    ("trigraph", "tests/test_trigraph.cpp", ["-trigraphs"]),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--preprocessor", required=True)
    parser.add_argument("--compiler", action="append", required=True,
                        help="version=path")
    args = parser.parse_args()
    compilers = []
    for item in args.compiler:
        version, path = item.split("=", 1)
        compilers.append((version, Path(path)))

    failures = 0
    with tempfile.TemporaryDirectory(prefix="crucible-matrix-") as temp:
        out = Path(temp)
        for version, compiler in compilers:
            for name, source, extra in CASES:
                pre = out / f"{name}-{version}.i"
                exe = out / f"{name}-{version}.exe"
                command = [
                    args.preprocessor, "--gcc", str(compiler), "-P",
                    *extra, str(REPO / source), "-o", str(pre),
                ]
                p1 = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
                p2 = subprocess.run(
                    [str(compiler), "-std=c++20", str(pre), "-o", str(exe)],
                    capture_output=True, text=True,
                )
                p3 = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8", errors="replace")
                ok = p1.returncode == 0 and p2.returncode == 0 and p3.returncode == 0
                print("%-4s %-5s %s" % ("PASS" if ok else "FAIL", version, name))
                if not ok:
                    failures += 1
                    print((p1.stderr + p2.stderr + p3.stderr)[:1200])
    print("%d failure(s)" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

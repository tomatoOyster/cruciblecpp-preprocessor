#!/usr/bin/env python3
"""Run differential tests against externally cached real projects.

Third-party sources are deliberately not stored in this repository. The cache
lives under tests/corpus-cache/ (gitignored), is downloaded by
fetch_realworld.py, and is only read by this runner.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DIFFERENTIAL = REPO / "tests" / "differential" / "run_differential.py"
spec = importlib.util.spec_from_file_location("differential", DIFFERENTIAL)
differential = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(differential)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc", required=True)
    parser.add_argument("--preprocessor", required=True)
    parser.add_argument("--preprocessor-arg", action="append", default=[])
    parser.add_argument("--manifest", default=str(HERE / "projects.json"))
    parser.add_argument("--cache", default=str(REPO / "tests" / "corpus-cache"))
    parser.add_argument("--filter", default="")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--report")
    args = parser.parse_args()

    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    cache = Path(args.cache).resolve()
    results = []

    for project in manifest:
        name = project["name"]
        if args.filter and args.filter not in name:
            continue
        project_cache = cache / name
        header = project_cache / project["header"]
        if not header.exists():
            print("SKIP  %-24s missing %s" % (name, header))
            results.append({"name": name, "status": "skip",
                            "reason": "cache missing"})
            continue

        include_root = project_cache / project.get("include", "")
        for case in project.get("cases", []):
            source = REPO / case["file"]
            case_args = list(case.get("args", []))
            case_args.extend(["-I", include_root.as_posix()])
            result = differential.run_case(
                {
                    "name": name + "_" + source.stem,
                    "file": str(source.relative_to(REPO)).replace("\\", "/"),
                    "args": case_args,
                    "std": case.get("std", "c++20"),
                    "expect_error": bool(case.get("expect_error", False)),
                },
                REPO,
                args.gcc,
                args.preprocessor,
                args.timeout,
                "line",
                args.preprocessor_arg,
                require_success=True,
            )
            status = "pass" if result["ok"] else "fail"
            results.append({"name": name + "_" + source.stem,
                            "status": status,
                            "problems": result.get("problems", [])})
            print("%-5s %-32s" % (status.upper(), name + "_" + source.stem))
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

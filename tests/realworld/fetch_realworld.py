#!/usr/bin/env python3
"""Download pinned real-project headers into the gitignored test cache."""

from __future__ import annotations

import argparse
import hashlib
import json
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", default=str(HERE / "projects.json"))
    parser.add_argument("--cache", default=str(REPO / "tests" / "corpus-cache"))
    args = parser.parse_args()

    cache = Path(args.cache).resolve()
    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    for project in manifest:
        destination = cache / project["name"] / project["header"]
        if destination.exists():
            actual = sha256(destination)
            if actual != project["sha256"]:
                raise SystemExit("hash mismatch for %s: %s" % (destination, actual))
            print("present %s" % destination)
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        print("download %s" % project["url"])
        with urllib.request.urlopen(project["url"], timeout=120) as response:
            data = response.read()
        destination.write_bytes(data)
        actual = sha256(destination)
        if actual != project["sha256"]:
            destination.unlink()
            raise SystemExit("hash mismatch for %s: %s" % (destination, actual))
        print("verified %s" % destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

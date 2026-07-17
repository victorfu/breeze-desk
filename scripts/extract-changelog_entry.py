#!/usr/bin/env python3
"""Print one Markdown release section from CHANGELOG.md."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("version")
    parser.add_argument("--file", default="CHANGELOG.md")
    args = parser.parse_args()
    text = pathlib.Path(args.file).read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^## \[{re.escape(args.version)}\].*?\n(?P<body>.*?)(?=^## \[|\Z)",
        re.MULTILINE | re.DOTALL,
    )
    match = pattern.search(text)
    if match is None:
        print(f"Version {args.version} is absent from {args.file}", file=sys.stderr)
        return 1
    print(match.group("body").strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

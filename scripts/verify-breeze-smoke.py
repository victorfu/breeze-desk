#!/usr/bin/env python3
"""Validate the self-hosted Breeze Q5 long-audio smoke-test export."""

from __future__ import annotations

import json
import pathlib
import sys


EXPECTED_Q5_SHA256 = "8efbf0ce8a3f50fe332b7617da787fb81354b358c288b008d3bdef8359df64c6"
MINIMUM_DURATION_MS = 30 * 60 * 1000


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: verify-breeze-smoke.py <result.json>", file=sys.stderr)
        return 2
    path = pathlib.Path(sys.argv[1])
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1:
        raise ValueError("unexpected transcript export schema")
    if data.get("recording", {}).get("durationMs", 0) < MINIMUM_DURATION_MS:
        raise ValueError("the self-hosted fixture must be at least 30 minutes")
    if data.get("engine", {}).get("modelChecksum") != EXPECTED_Q5_SHA256:
        raise ValueError("the smoke test did not use the manifest-pinned Breeze Q5 model")
    segments = data.get("segments", [])
    if not segments:
        raise ValueError("the long-audio smoke test returned no transcript segments")
    previous_start = -1
    for segment in segments:
        start = segment.get("startMs", -1)
        end = segment.get("endMs", -1)
        if start < previous_start or end <= start:
            raise ValueError("transcript segment timestamps are invalid or unordered")
        previous_start = start
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

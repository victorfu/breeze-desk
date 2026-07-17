#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
if [[ ! -f "$project_dir/build/debug/CMakeCache.txt" ]]; then
  "$project_dir/scripts/build.sh"
fi
ctest --test-dir "$project_dir/build/debug" --output-on-failure "$@"

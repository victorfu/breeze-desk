#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
"$project_dir/scripts/build.sh"
app_name="$(cmake -N -LA "$project_dir/build/debug" | sed -n 's/^BREEZEDESK_DEBUG_EXECUTABLE_NAME:STRING=//p')"
if [[ -z "$app_name" ]]; then
  echo "Unable to read BREEZEDESK_DEBUG_EXECUTABLE_NAME from the CMake cache." >&2
  exit 1
fi
if [[ -d "$project_dir/build/debug/src/app/$app_name.app" ]]; then
  exec "$project_dir/build/debug/src/app/$app_name.app/Contents/MacOS/$app_name" "$@"
fi
exec "$project_dir/build/debug/src/app/$app_name" "$@"

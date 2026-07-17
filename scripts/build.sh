#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
export CCACHE_DIR="${CCACHE_DIR:-$project_dir/.cache/ccache}"
extra_args=()
if [[ "$(uname -s)" == "Darwin" && -z "${CMAKE_OSX_DEPLOYMENT_TARGET:-}" ]]; then
  host_macos_version="$(sw_vers -productVersion)"
  host_macos_major="${host_macos_version%%.*}"
  extra_args+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=${host_macos_major}.0")
fi
if [[ -n "${BREEZEDESK_WHISPER_CPP_SOURCE_DIR:-}" ]]; then
  extra_args+=("-DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=${BREEZEDESK_WHISPER_CPP_SOURCE_DIR}")
fi
cmake --preset debug -S "$project_dir" "${extra_args[@]}"
cmake --build --preset debug --parallel

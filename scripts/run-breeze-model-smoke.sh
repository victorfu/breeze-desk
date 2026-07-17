#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${BREEZEDESK_BREEZE_MODEL_PATH:-}" || -z "${BREEZEDESK_LONG_AUDIO_FIXTURE:-}" ]]; then
  echo "BREEZEDESK_BREEZE_MODEL_PATH and BREEZEDESK_LONG_AUDIO_FIXTURE are required" >&2
  exit 2
fi
cmake -S "$project_dir" -B "$project_dir/build/model-smoke" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBREEZEDESK_ENABLE_WHISPER=ON -DBREEZEDESK_BUILD_TESTS=OFF \
  "-DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=${BREEZEDESK_WHISPER_CPP_SOURCE_DIR:-}"
cmake --build "$project_dir/build/model-smoke" --parallel
identity_dir="$project_dir/build/model-smoke/identity"
cmake -DBREEZEDESK_SOURCE_DIR="$project_dir" \
  -DBREEZEDESK_IDENTITY_OUTPUT_DIR="$identity_dir" \
  -P "$project_dir/cmake/ReadProjectIdentity.cmake"
cli_name="$(tr -d '\r\n' < "$identity_dir/cli-executable-name.txt")"
cli="$project_dir/build/model-smoke/src/cli/$cli_name"
[[ -x "$cli" ]] || { echo "Built CLI is missing: $cli" >&2; exit 1; }
result="$project_dir/build/model-smoke/smoke-result.json"
BREEZEDESK_DATA_ROOT="$project_dir/build/model-smoke/data" "$cli" \
  transcribe "$BREEZEDESK_LONG_AUDIO_FIXTURE" \
  --headless --model-path "$BREEZEDESK_BREEZE_MODEL_PATH" --language zh --preset balanced \
  --format json --output "$result" --json
python3 "$project_dir/scripts/verify-breeze-smoke.py" "$result"

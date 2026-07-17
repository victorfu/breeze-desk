#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
qt_path="${QT_HOST_PATH:-}"
lrelease_executable="${qt_path:+$qt_path/bin/}lrelease"
"$lrelease_executable" "$project_dir/translations/breezedesk_en.ts" "$project_dir/translations/breezedesk_zh_TW.ts"

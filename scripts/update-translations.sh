#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
qt_path="${QT_HOST_PATH:-}"
lupdate_executable="${qt_path:+$qt_path/bin/}lupdate"
"$lupdate_executable" "$project_dir/src" "$project_dir/include" \
  -ts "$project_dir/translations/breezedesk_en.ts" "$project_dir/translations/breezedesk_zh_TW.ts"

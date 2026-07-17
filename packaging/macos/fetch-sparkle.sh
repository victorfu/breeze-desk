#!/usr/bin/env bash
set -euo pipefail

version="2.9.2"
revision="6276ba2b404829d139c45ff98427cf90e2efc59b"
sha256="1cb340cbbef04c6c0d162078610c25e2221031d794a3449d89f2f56f4df77c95"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work_dir="$project_dir/build/sparkle-$version"
archive="$work_dir/Sparkle-$version.tar.xz"
install_dir="$work_dir/install"

cached_install=0
if [[ -d "$install_dir/Sparkle.framework" && -x "$install_dir/bin/sign_update" &&
      -f "$install_dir/LICENSE" && -f "$install_dir/SOURCE.txt" ]]; then
  cached_install=1
fi
for command_name in curl shasum tar; do
  command -v "$command_name" >/dev/null 2>&1 || {
    echo "Required command '$command_name' was not found" >&2
    exit 1
  }
done

cmake -E make_directory "$work_dir"
if [[ ! -f "$archive" ]]; then
  curl --fail --location --retry 3 \
    --output "$archive.part" \
    "https://github.com/sparkle-project/Sparkle/releases/download/$version/Sparkle-$version.tar.xz"
  mv "$archive.part" "$archive"
fi
actual_sha="$(shasum -a 256 "$archive" | awk '{print $1}')"
if [[ "$actual_sha" != "$sha256" ]]; then
  echo "Sparkle release checksum mismatch: expected $sha256, received $actual_sha" >&2
  exit 1
fi
if [[ "$cached_install" == "1" ]]; then
  echo "$install_dir"
  exit 0
fi
cmake -E remove_directory "$install_dir"
cmake -E make_directory "$install_dir"
tar -xf "$archive" -C "$install_dir"
printf '%s\n' \
  "Sparkle $version" \
  "Source revision: $revision" \
  "Release: https://github.com/sparkle-project/Sparkle/releases/tag/$version" \
  "Archive SHA-256: $sha256" > "$install_dir/SOURCE.txt"
[[ -d "$install_dir/Sparkle.framework" && -x "$install_dir/bin/sign_update" ]] || {
  echo "Sparkle release archive has an unexpected layout" >&2
  exit 1
}
echo "$install_dir"

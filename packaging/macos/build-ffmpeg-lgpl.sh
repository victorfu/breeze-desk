#!/usr/bin/env bash
set -euo pipefail

version="8.1.2"
sha256="464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c"
deployment_target="${BREEZEDESK_MACOS_DEPLOYMENT_TARGET:-14.0}"
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
work_dir="$project_dir/build/ffmpeg-$version-macos-arm64"
archive="$work_dir/ffmpeg-$version.tar.xz"
source_dir="$work_dir/source"
install_dir="$work_dir/install"

[[ "$deployment_target" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]] || {
  echo "Invalid BREEZEDESK_MACOS_DEPLOYMENT_TARGET: $deployment_target" >&2
  exit 1
}

cached_install=0
if [[ -x "$install_dir/bin/ffmpeg" && -x "$install_dir/bin/ffprobe" &&
      -f "$install_dir/BUILD_CONFIGURATION.txt" && -f "$install_dir/SOURCE.txt" &&
      -f "$install_dir/DEPLOYMENT_TARGET.txt" &&
      -f "$install_dir/LICENSES/FFmpeg-LGPL-2.1.txt" &&
      -f "$install_dir/LICENSES/FFmpeg-LGPL-3.0.txt" ]]; then
  if grep -Eq -- '--enable-(gpl|nonfree)' "$install_dir/BUILD_CONFIGURATION.txt"; then
    echo "Cached FFmpeg build is not LGPL-compatible" >&2
    exit 1
  fi
  if [[ "$(tr -d '\r\n' < "$install_dir/DEPLOYMENT_TARGET.txt")" == "$deployment_target" ]]; then
    cached_install=1
  fi
fi

for command_name in curl shasum tar make clang; do
  command -v "$command_name" >/dev/null 2>&1 || {
    echo "Required command '$command_name' was not found" >&2
    exit 1
  }
done

cmake -E make_directory "$work_dir"
if [[ ! -f "$archive" ]]; then
  curl --fail --location --retry 3 --output "$archive.part" "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
  mv "$archive.part" "$archive"
fi
actual_sha="$(shasum -a 256 "$archive" | awk '{print $1}')"
if [[ "$actual_sha" != "$sha256" ]]; then
  echo "FFmpeg source checksum mismatch" >&2
  exit 1
fi
if [[ "$cached_install" == "1" ]]; then
  echo "$install_dir/bin"
  exit 0
fi
cmake -E remove_directory "$source_dir"
cmake -E remove_directory "$install_dir"
cmake -E make_directory "$source_dir"
cmake -E make_directory "$install_dir"
tar -xf "$archive" --strip-components=1 -C "$source_dir"
(
  cd "$source_dir"
  export MACOSX_DEPLOYMENT_TARGET="$deployment_target"
  ./configure \
    --prefix="$install_dir" \
    --arch=arm64 \
    --target-os=darwin \
    --cc=clang \
    --disable-gpl \
    --disable-nonfree \
    --disable-network \
    --disable-autodetect \
    --disable-doc \
    --disable-debug \
    --disable-shared \
    --enable-static \
    --enable-small \
    --extra-cflags="-mmacosx-version-min=$deployment_target" \
    --extra-ldflags="-mmacosx-version-min=$deployment_target"
  make -j"$(sysctl -n hw.logicalcpu)"
  make install
)
cmake -E make_directory "$install_dir/LICENSES"
cp "$source_dir/COPYING.LGPLv2.1" "$install_dir/LICENSES/FFmpeg-LGPL-2.1.txt"
cp "$source_dir/COPYING.LGPLv3" "$install_dir/LICENSES/FFmpeg-LGPL-3.0.txt"
"$install_dir/bin/ffmpeg" -hide_banner -buildconf > "$install_dir/BUILD_CONFIGURATION.txt" 2>&1
printf '%s\n' "FFmpeg $version" "Source: https://ffmpeg.org/releases/ffmpeg-$version.tar.xz" \
  "SHA-256: $sha256" > "$install_dir/SOURCE.txt"
printf '%s\n' "$deployment_target" > "$install_dir/DEPLOYMENT_TARGET.txt"
echo "$install_dir/bin"

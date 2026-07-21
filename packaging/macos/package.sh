#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
command -v cmake >/dev/null 2>&1 || { echo "cmake is required for macOS packaging" >&2; exit 1; }
identity_dir="$project_dir/build/package-identity"
cmake -DBREEZEDESK_SOURCE_DIR="$project_dir" \
  -DBREEZEDESK_IDENTITY_OUTPUT_DIR="$identity_dir" \
  -P "$project_dir/cmake/ReadProjectIdentity.cmake"
product_name="${BREEZEDESK_PRODUCT_NAME:-$(tr -d '\r\n' < "$identity_dir/product-name.txt")}"
release_executable_name="${BREEZEDESK_RELEASE_EXECUTABLE_NAME:-$(tr -d '\r\n' < "$identity_dir/release-executable-name.txt")}"
worker_executable_name="${BREEZEDESK_WORKER_EXECUTABLE_NAME:-$(tr -d '\r\n' < "$identity_dir/worker-executable-name.txt")}"
cli_executable_name="${BREEZEDESK_CLI_EXECUTABLE_NAME:-$(tr -d '\r\n' < "$identity_dir/cli-executable-name.txt")}"
build_dir="$project_dir/build/release"
stage_dir="$project_dir/build/package-macos"
dmg_root="$project_dir/build/dmg-root"
dist_dir="$project_dir/dist"
version_file="$project_dir/build/package-macos-version.txt"
app="$stage_dir/$release_executable_name.app"
whisper_cpp_ref="f049fff95a089aa9969deb009cdd4892b3e74916"
deployment_target="${BREEZEDESK_MACOS_DEPLOYMENT_TARGET:-14.0}"

fail() {
  echo "$product_name macOS packaging: $*" >&2
  exit 1
}

version_is_at_most() {
  local actual="$1"
  local maximum="$2"
  local -a actual_parts maximum_parts
  local index actual_value maximum_value
  IFS=. read -r -a actual_parts <<<"$actual"
  IFS=. read -r -a maximum_parts <<<"$maximum"
  for index in 0 1 2; do
    actual_value="${actual_parts[$index]:-0}"
    maximum_value="${maximum_parts[$index]:-0}"
    if ((10#$actual_value < 10#$maximum_value)); then
      return 0
    fi
    if ((10#$actual_value > 10#$maximum_value)); then
      return 1
    fi
  done
  return 0
}

validate_macho_deployment_target() {
  local candidate="$1"
  local build_info actual
  local found=0
  file "$candidate" | grep -q 'Mach-O' || return 0
  build_info="$(xcrun vtool -show-build "$candidate" 2>&1)" || \
    fail "could not inspect the deployment target for $candidate: $build_info"
  while IFS= read -r actual; do
    [[ -n "$actual" ]] || continue
    found=1
    version_is_at_most "$actual" "$deployment_target" || \
      fail "$candidate requires macOS $actual, newer than the supported $deployment_target target"
  done < <(awk '$1 == "minos" { print $2 }' <<<"$build_info")
  ((found == 1)) || fail "could not find a macOS deployment target in $candidate"
}

[[ "$deployment_target" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]] || \
  fail "invalid BREEZEDESK_MACOS_DEPLOYMENT_TARGET: $deployment_target"

for command_name in cmake macdeployqt hdiutil xcrun lipo otool magick iconutil ditto file; do
  command -v "$command_name" >/dev/null 2>&1 || fail "required command '$command_name' was not found"
done
[[ "$(uname -s)" == "Darwin" ]] || fail "package.sh must run on macOS"
[[ "$(uname -m)" == "arm64" ]] || fail "the supported macOS package must be built on an arm64 host"
if [[ -n "${BREEZEDESK_WHISPER_CPP_SOURCE_DIR:-}" ]]; then
  command -v git >/dev/null 2>&1 || fail "git is required to verify BREEZEDESK_WHISPER_CPP_SOURCE_DIR"
  whisper_actual_ref="$(git -C "$BREEZEDESK_WHISPER_CPP_SOURCE_DIR" rev-parse HEAD 2>/dev/null || true)"
  [[ "$whisper_actual_ref" == "$whisper_cpp_ref" ]] || \
    fail "BREEZEDESK_WHISPER_CPP_SOURCE_DIR is $whisper_actual_ref, expected $whisper_cpp_ref"
fi
[[ -n "${BREEZEDESK_FFMPEG_DIR:-}" ]] || \
  fail "BREEZEDESK_FFMPEG_DIR must point to an LGPL FFmpeg bin directory"
for tool in ffmpeg ffprobe; do
  [[ -x "$BREEZEDESK_FFMPEG_DIR/$tool" ]] || fail "missing executable $BREEZEDESK_FFMPEG_DIR/$tool"
  lipo -archs "$BREEZEDESK_FFMPEG_DIR/$tool" | grep -qw arm64 || \
    fail "$tool does not contain an arm64 slice"
done

cmake -E make_directory "$project_dir/build"
cmake -DBREEZEDESK_SOURCE_DIR="$project_dir" \
  -DBREEZEDESK_VERSION_OUTPUT="$version_file" \
  -P "$project_dir/cmake/ReadProjectVersion.cmake"
version="$(tr -d '\r\n' < "$version_file")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "CMake project version is invalid: '$version'"

ffmpeg_configuration="$("$BREEZEDESK_FFMPEG_DIR/ffmpeg" -hide_banner -buildconf 2>&1)"
if grep -Eq -- '--enable-(gpl|nonfree)' <<<"$ffmpeg_configuration"; then
  fail "the selected FFmpeg build enables GPL or nonfree components"
fi

configure_args=(
  --preset release
  -S "$project_dir"
  -DCMAKE_OSX_ARCHITECTURES=arm64
  "-DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment_target"
  "-DBREEZEDESK_PRODUCT_NAME=$product_name"
  "-DBREEZEDESK_RELEASE_EXECUTABLE_NAME=$release_executable_name"
  "-DBREEZEDESK_WORKER_EXECUTABLE_NAME=$worker_executable_name"
  "-DBREEZEDESK_CLI_EXECUTABLE_NAME=$cli_executable_name"
  -DBREEZEDESK_WINDOWS_BACKEND=CPU
  -DBREEZEDESK_ENABLE_WHISPER=ON
  -DBREEZEDESK_BUILD_TESTS=OFF
  "-DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=${BREEZEDESK_WHISPER_CPP_SOURCE_DIR:-}"
)
if [[ "${BREEZEDESK_PACKAGE_UPDATES:-0}" == "1" ]]; then
  [[ -n "${BREEZEDESK_APPCAST_URL:-}" ]] || fail "BREEZEDESK_APPCAST_URL is required when updates are enabled"
  [[ -n "${BREEZEDESK_EDDSA_PUBLIC_KEY:-}" ]] || \
    fail "BREEZEDESK_EDDSA_PUBLIC_KEY is required when updates are enabled"
  [[ -n "${BREEZEDESK_SPARKLE_FRAMEWORK_DIR:-}" ]] || \
    fail "BREEZEDESK_SPARKLE_FRAMEWORK_DIR is required when updates are enabled"
  configure_args+=(
    -DBREEZEDESK_ENABLE_UPDATES=ON
    "-DBREEZEDESK_APPCAST_URL=$BREEZEDESK_APPCAST_URL"
    "-DBREEZEDESK_EDDSA_PUBLIC_KEY=$BREEZEDESK_EDDSA_PUBLIC_KEY"
  )
else
  configure_args+=(-DBREEZEDESK_ENABLE_UPDATES=OFF)
fi

cmake "${configure_args[@]}"
cmake --build --preset release --parallel
cmake -E remove_directory "$stage_dir"
cmake -E remove_directory "$dmg_root"
cmake -E make_directory "$stage_dir" "$dmg_root" "$dist_dir"
cmake --install "$build_dir" --prefix "$stage_dir"
[[ -d "$app" ]] || fail "CMake install did not produce $app"

app_macos="$app/Contents/MacOS"
app_resources="$app/Contents/Resources"
license_dir="$app_resources/licenses"
cmake -E make_directory "$license_dir"
ffmpeg_license_dir="${BREEZEDESK_FFMPEG_LICENSE_DIR:-$(cd "$BREEZEDESK_FFMPEG_DIR/.." && pwd)/LICENSES}"
[[ -f "$ffmpeg_license_dir/FFmpeg-LGPL-2.1.txt" &&
   -f "$ffmpeg_license_dir/FFmpeg-LGPL-3.0.txt" ]] || \
  fail "FFmpeg LGPL texts are missing; set BREEZEDESK_FFMPEG_LICENSE_DIR or use build-ffmpeg-lgpl.sh"
whisper_license=""
if [[ -n "${BREEZEDESK_WHISPER_CPP_SOURCE_DIR:-}" &&
      -f "$BREEZEDESK_WHISPER_CPP_SOURCE_DIR/LICENSE" ]]; then
  whisper_license="$BREEZEDESK_WHISPER_CPP_SOURCE_DIR/LICENSE"
fi
for candidate in "$build_dir/_deps/whisper_cpp-src/LICENSE" \
                 "$build_dir/_deps/whisper.cpp-src/LICENSE"; do
  [[ -z "$whisper_license" ]] || break
  if [[ -f "$candidate" ]]; then
    whisper_license="$candidate"
    break
  fi
done
[[ -n "$whisper_license" ]] || fail "the pinned whisper.cpp LICENSE file could not be located"
worker="$app_macos/$worker_executable_name"
[[ -x "$worker" ]] || fail "the native libwhisper worker is missing from the app bundle"
cli_source="$stage_dir/bin/$cli_executable_name"
[[ -x "$cli_source" ]] || fail "CMake install did not produce $cli_executable_name"
cli="$app_macos/$cli_executable_name"
cp "$cli_source" "$cli"
cp "$BREEZEDESK_FFMPEG_DIR/ffmpeg" "$BREEZEDESK_FFMPEG_DIR/ffprobe" "$app_macos/"
cp "$project_dir/LICENSE" "$license_dir/$product_name-MIT.txt"
cp "$project_dir/THIRD_PARTY_NOTICES.md" "$license_dir/THIRD_PARTY_NOTICES.md"
cp "$project_dir/docs/licenses/Qt-LGPL-NOTICE.md" "$license_dir/Qt-LGPL-NOTICE.md"
cp "$project_dir/docs/licenses/LGPL-3.0-only.txt" "$license_dir/LGPL-3.0-only.txt"
cp "$project_dir/docs/licenses/GPL-3.0-only.txt" "$license_dir/GPL-3.0-only.txt"
cp "$build_dir/generated/Qt-SOURCE.txt" "$license_dir/Qt-SOURCE.txt"
cp "$whisper_license" "$license_dir/whisper.cpp-MIT.txt"
cp "$project_dir/resources/icons/lucide/LICENSE" "$license_dir/Lucide-LICENSE.txt"
cp "$project_dir/resources/icons/lucide/SOURCE.md" "$license_dir/Lucide-SOURCE.md"
cp "$ffmpeg_license_dir/FFmpeg-LGPL-2.1.txt" "$license_dir/FFmpeg-LGPL-2.1.txt"
cp "$ffmpeg_license_dir/FFmpeg-LGPL-3.0.txt" "$license_dir/FFmpeg-LGPL-3.0.txt"
cp "$project_dir/resources/models/models.json" "$app_resources/models.json"
printf '%s\n' "$ffmpeg_configuration" > "$license_dir/FFmpeg-BUILD_CONFIGURATION.txt"
{
  "$BREEZEDESK_FFMPEG_DIR/ffmpeg" -hide_banner -version
  echo "Source archive: https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz"
  echo "Source SHA-256: 464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c"
} > "$license_dir/FFmpeg-SOURCE.txt" 2>&1

iconset="$stage_dir/AppIcon.iconset"
cmake -E make_directory "$iconset"
for size in 16 32 128 256 512; do
  magick -background transparent "$project_dir/resources/icons/breezedesk.png" \
    -filter Lanczos -resize "${size}x${size}" "$iconset/icon_${size}x${size}.png"
  double_size=$((size * 2))
  magick -background transparent "$project_dir/resources/icons/breezedesk.png" \
    -filter Lanczos -resize "${double_size}x${double_size}" "$iconset/icon_${size}x${size}@2x.png"
done
cmake -E rm -f "$app_resources/breezedesk.icns"
iconutil -c icns "$iconset" -o "$app_resources/breezedesk.icns"

macdeployqt "$app" -qmldir="$project_dir/src/qml" -always-overwrite -verbose=2 \
  -executable="$worker" -executable="$cli"

for executable in "$app_macos/$release_executable_name" "$worker" "$cli" \
  "$app_macos/ffmpeg" "$app_macos/ffprobe"; do
  [[ -x "$executable" ]] || fail "required bundled executable is missing: $executable"
  lipo -archs "$executable" | grep -qw arm64 || fail "$(basename "$executable") is not arm64"
done
otool -L "$app_macos/$release_executable_name" > "$license_dir/$product_name-DYNAMIC_LIBRARIES.txt"
otool -L "$worker" > "$license_dir/ASR-Worker-DYNAMIC_LIBRARIES.txt"

if [[ -n "${BREEZEDESK_SPARKLE_FRAMEWORK_DIR:-}" ]]; then
  [[ "${BREEZEDESK_PACKAGE_UPDATES:-0}" == "1" ]] || \
    fail "a Sparkle framework was supplied but BREEZEDESK_PACKAGE_UPDATES is not 1"
  [[ -d "$BREEZEDESK_SPARKLE_FRAMEWORK_DIR/Sparkle.framework" ]] || \
    fail "BREEZEDESK_SPARKLE_FRAMEWORK_DIR must contain Sparkle.framework"
  ditto "$BREEZEDESK_SPARKLE_FRAMEWORK_DIR/Sparkle.framework" \
    "$app/Contents/Frameworks/Sparkle.framework"
  lipo -archs "$app/Contents/Frameworks/Sparkle.framework/Sparkle" | grep -qw arm64 || \
    fail "Sparkle.framework does not contain an arm64 slice"
  [[ -f "$BREEZEDESK_SPARKLE_FRAMEWORK_DIR/LICENSE" ]] && \
    cp "$BREEZEDESK_SPARKLE_FRAMEWORK_DIR/LICENSE" "$license_dir/Sparkle-LICENSE.txt"
  [[ -f "$BREEZEDESK_SPARKLE_FRAMEWORK_DIR/SOURCE.txt" ]] && \
    cp "$BREEZEDESK_SPARKLE_FRAMEWORK_DIR/SOURCE.txt" "$license_dir/Sparkle-SOURCE.txt"
  /usr/libexec/PlistBuddy -c "Delete :SUFeedURL" "$app/Contents/Info.plist" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :SUFeedURL string $BREEZEDESK_APPCAST_URL" "$app/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c "Delete :SUPublicEDKey" "$app/Contents/Info.plist" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :SUPublicEDKey string $BREEZEDESK_EDDSA_PUBLIC_KEY" "$app/Contents/Info.plist"
fi

while IFS= read -r -d '' candidate; do
  validate_macho_deployment_target "$candidate"
done < <(find "$app/Contents" -type f -print0)

if [[ -n "${BREEZEDESK_CODESIGN_IDENTITY:-}" ]]; then
  command -v codesign >/dev/null 2>&1 || fail "codesign was not found"
  codesign_args=(
    --force
    --options runtime
    --timestamp
    --preserve-metadata=entitlements,requirements
    --sign "$BREEZEDESK_CODESIGN_IDENTITY"
  )
  while IFS= read -r -d '' candidate; do
    if file "$candidate" | grep -q 'Mach-O'; then
      codesign "${codesign_args[@]}" "$candidate"
    fi
  done < <(find "$app/Contents" -type f -print0)
  while IFS= read -r nested_bundle; do
    codesign "${codesign_args[@]}" "$nested_bundle"
  done < <(find "$app/Contents" -type d \( -name '*.app' -o -name '*.xpc' \) -print | \
    awk '{ print length($0), $0 }' | sort -rn | cut -d' ' -f2-)
  while IFS= read -r framework; do
    codesign "${codesign_args[@]}" "$framework"
  done < <(find "$app/Contents" -type d -name '*.framework' | sort -r)
  codesign "${codesign_args[@]}" "$app"
  codesign --verify --deep --strict --verbose=2 "$app"
elif [[ -n "${BREEZEDESK_NOTARY_PROFILE:-}" ]]; then
  fail "BREEZEDESK_CODESIGN_IDENTITY is required when notarization is requested"
fi

cp -R "$app" "$dmg_root/"
ln -s /Applications "$dmg_root/Applications"
dmg="$dist_dir/$product_name-$version-macOS-arm64.dmg"
cmake -E rm -f "$dmg" "$dmg.sha256"
hdiutil create -volname "$product_name" -srcfolder "$dmg_root" -ov -format UDZO "$dmg"
if [[ -n "${BREEZEDESK_CODESIGN_IDENTITY:-}" ]]; then
  codesign --force --timestamp --sign "$BREEZEDESK_CODESIGN_IDENTITY" "$dmg"
fi
if [[ -n "${BREEZEDESK_NOTARY_PROFILE:-}" ]]; then
  xcrun notarytool submit "$dmg" --keychain-profile "$BREEZEDESK_NOTARY_PROFILE" --wait
  xcrun stapler staple "$dmg"
  xcrun stapler validate "$dmg"
  spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
fi
hdiutil verify "$dmg"
(
  cd "$dist_dir"
  shasum -a 256 "$(basename "$dmg")" > "$(basename "$dmg").sha256"
)
echo "$dmg"

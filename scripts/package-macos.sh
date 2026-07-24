#!/usr/bin/env bash
# Package a macOS DMG in one step from local credentials.
#
# Loads Apple Developer and Sparkle secrets from .env.package (git-ignored),
# prepares the pinned LGPL FFmpeg and Sparkle sidecars, resolves a Developer ID
# identity and a notarization profile, then builds, signs, notarizes, and signs
# the Sparkle update enclosure for dist/<Product>-<version>-macOS-arm64.dmg.
#
# With no signing credentials it still produces an unsigned local DMG, which is
# useful for verifying the build but is blocked by Gatekeeper on other Macs.
#
# Point at a different env file with:
#   BREEZEDESK_ENV_FILE=/path/to/env ./scripts/package-macos.sh
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_dir"

# Fail fast on the one packaging tool Homebrew provides (ImageMagick renders the
# app icon); the rest of package.sh's prerequisites ship with Xcode and Qt.
if ! command -v magick >/dev/null 2>&1; then
  echo "ImageMagick is required to render the app icon. Install it with:" >&2
  echo "  brew install imagemagick" >&2
  exit 1
fi

env_file="${BREEZEDESK_ENV_FILE:-$project_dir/.env.package}"
if [[ -f "$env_file" ]]; then
  echo "Loading credentials from ${env_file#"$project_dir/"}"
  set -a
  # shellcheck source=/dev/null
  source "$env_file"
  set +a
fi

echo "Preparing pinned FFmpeg (LGPL) and Sparkle sidecars..."
export BREEZEDESK_FFMPEG_DIR="${BREEZEDESK_FFMPEG_DIR:-$(packaging/macos/build-ffmpeg-lgpl.sh)}"
sparkle_dir="$(packaging/macos/fetch-sparkle.sh)"
export BREEZEDESK_SPARKLE_FRAMEWORK_DIR="$sparkle_dir"
export BREEZEDESK_SPARKLE_SIGN_UPDATE="$sparkle_dir/bin/sign_update"

find_identity() {
  { security find-identity -v -p codesigning 2>/dev/null || true; } |
    awk -F'"' '/Developer ID Application/ { print $2; exit }'
}

# Resolve the Developer ID signing identity: honor an explicit override, else
# discover one already in the keychain, importing the .env.package certificate
# first only if none is present.
if [[ -z "${BREEZEDESK_CODESIGN_IDENTITY:-}" ]]; then
  identity="$(find_identity)"
  if [[ -z "$identity" && -n "${APPLE_CERTIFICATE:-}" && -n "${APPLE_CERTIFICATE_PASSWORD:-}" ]]; then
    echo "Importing the Developer ID certificate into the login keychain..."
    p12="$(mktemp)"
    printf '%s' "$APPLE_CERTIFICATE" | base64 --decode >"$p12"
    security import "$p12" -P "$APPLE_CERTIFICATE_PASSWORD" -T /usr/bin/codesign >/dev/null
    rm -f "$p12"
    identity="$(find_identity)"
  fi
  [[ -n "$identity" ]] && export BREEZEDESK_CODESIGN_IDENTITY="$identity"
fi

if [[ -n "${BREEZEDESK_CODESIGN_IDENTITY:-}" ]]; then
  echo "Signing identity: $BREEZEDESK_CODESIGN_IDENTITY"
  # Create a notarization profile from Apple credentials when one is not named.
  if [[ -z "${BREEZEDESK_NOTARY_PROFILE:-}" ]]; then
    if [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
      export BREEZEDESK_NOTARY_PROFILE="breezedesk-notary"
      echo "Storing notarization credentials in keychain profile '$BREEZEDESK_NOTARY_PROFILE'..."
      xcrun notarytool store-credentials "$BREEZEDESK_NOTARY_PROFILE" \
        --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" >/dev/null
    else
      echo "No Apple ID credentials found — the DMG will be signed but not notarized." >&2
    fi
  fi
  # Enable Sparkle updates when a public key is available.
  if [[ -n "${BREEZEDESK_EDDSA_PUBLIC_KEY:-}" ]]; then
    export BREEZEDESK_PACKAGE_UPDATES=1
    : "${BREEZEDESK_APPCAST_URL:=https://github.com/victorfu/breeze-desk/releases/latest/download/appcast-macos.xml}"
    export BREEZEDESK_APPCAST_URL
  fi
else
  echo "No Developer ID identity available — building an UNSIGNED local DMG."
  echo "(Gatekeeper blocks unsigned apps on other Macs; use this only for local checks.)"
fi

echo "Building the DMG..."
packaging/macos/package.sh

version="$(tr -d '\r\n' <build/package-macos-version.txt)"
product_name="${BREEZEDESK_PRODUCT_NAME:-$(tr -d '\r\n' <build/package-identity/product-name.txt)}"
dmg="dist/$product_name-$version-macOS-arm64.dmg"

if [[ "${BREEZEDESK_PACKAGE_UPDATES:-0}" == "1" && -n "${BREEZEDESK_SPARKLE_PRIVATE_KEY:-}" ]]; then
  echo "Signing the Sparkle update enclosure..."
  packaging/macos/sign-sparkle-update.sh "$dmg"
fi

echo
echo "Packaged: $dmg"
[[ -f "$dmg.sha256" ]] && echo "Checksum: $dmg.sha256"
[[ -f "$dmg.edSignature" ]] && echo "Update signature: $dmg.edSignature"

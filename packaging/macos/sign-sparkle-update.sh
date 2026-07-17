#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: sign-sparkle-update.sh <update.dmg>" >&2
  exit 2
fi
artifact="$1"
[[ -f "$artifact" ]] || { echo "Update artifact does not exist: $artifact" >&2; exit 1; }
[[ -n "${BREEZEDESK_SPARKLE_SIGN_UPDATE:-}" ]] || {
  echo "BREEZEDESK_SPARKLE_SIGN_UPDATE must name Sparkle's pinned sign_update executable" >&2
  exit 1
}
[[ -x "$BREEZEDESK_SPARKLE_SIGN_UPDATE" ]] || {
  echo "Sparkle sign_update is not executable: $BREEZEDESK_SPARKLE_SIGN_UPDATE" >&2
  exit 1
}
[[ -n "${BREEZEDESK_SPARKLE_PRIVATE_KEY:-}" ]] || {
  echo "BREEZEDESK_SPARKLE_PRIVATE_KEY is required; it is passed through stdin only" >&2
  exit 1
}

signature_file="$artifact.edSignature"
printf '%s' "$BREEZEDESK_SPARKLE_PRIVATE_KEY" | \
  "$BREEZEDESK_SPARKLE_SIGN_UPDATE" --ed-key-file - "$artifact" > "$signature_file"
grep -Eq 'sparkle:edSignature="[^"]+"' "$signature_file" || {
  echo "Sparkle sign_update did not return an EdDSA signature fragment" >&2
  exit 1
}
echo "$signature_file"

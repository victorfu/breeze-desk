#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "$script_directory/.." && pwd)"
destination="${1:-$repository_root/build/test-models}"
mkdir -p "$destination"

# Immutable Hugging Face repository revision. The SHA-256 is the Xet/LFS
# object digest published on the file page at this revision.
model_revision="c521a4b02f422512d734391fdf08bb08c0862f68"
model_name="ggml-tiny.en-q5_1.bin"
expected_sha256="c77c5766f1cef09b6b7d47f21b546cbddd4157886b3b5d6d4f709e91e66c7c2b"
model_url="https://huggingface.co/ggerganov/whisper.cpp/resolve/${model_revision}/${model_name}"
output="$destination/$model_name"

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "Neither sha256sum nor shasum is available." >&2
        return 127
    fi
}

if [[ -f "$output" ]]; then
    actual_sha256="$(sha256_file "$output")"
    if [[ "$actual_sha256" == "$expected_sha256" ]]; then
        printf '%s\n' "$output"
        exit 0
    fi
    timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
    mv "$output" "$output.checksum-failed-$timestamp"
fi

curl --fail --location --continue-at - --output "$output.part" "$model_url"
actual_sha256="$(sha256_file "$output.part")"
if [[ "$actual_sha256" != "$expected_sha256" ]]; then
    timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
    failed_output="$output.part.checksum-failed-$timestamp"
    mv "$output.part" "$failed_output"
    echo "Test model checksum mismatch." >&2
    echo "Expected: $expected_sha256" >&2
    echo "Actual:   $actual_sha256" >&2
    echo "Saved as: $failed_output" >&2
    exit 1
fi

mv "$output.part" "$output"
printf '%s\n' "$output"

# BreezeDesk

BreezeDesk is a private, offline desktop workspace for long-form transcription. It imports audio and
video, records a microphone, normalizes media with an LGPL FFmpeg sidecar, and transcribes through a
separate native Qt worker that links directly to the whisper.cpp C API. Audio and transcripts remain
on the computer; the only network features are explicit model downloads and optional update checks.

The first release targets Taiwan Mandarin, mixed Chinese/English engineering meetings, macOS 14+
on Apple Silicon, and Windows 10 22H2+/Windows 11 x64.

## Highlights

- Crash-isolated `breezedesk-asr-worker`; no `whisper-cli`, cloud ASR, Python runtime, or telemetry.
- Resumable long-form jobs with VAD-aware chunks, partial segment persistence, cancellation, retry,
  and deterministic overlap handling.
- SQLite library, transcript revisions and editing, tags, search, trash, glossary profiles, prompt
  budgeting, and auditable alias replacement.
- QML interface with semantic design tokens, light/dark/system themes, English and Traditional
  Chinese, keyboard navigation, waveform/player synchronization, and accessible controls.
- Q5/Q8/custom GGML model management, verified resumable downloads, CLI exports, native packages,
  and optional Sparkle/WinSparkle updates.

## Build on macOS

Install CMake, Ninja, Qt 6.8+ (Qt 6.10.1 is the release baseline), Xcode Command Line Tools, and an
LGPL-compatible FFmpeg development sidecar. Then run:

```sh
./scripts/build.sh
./scripts/run-tests.sh
./scripts/build-and-run.sh
```

To reuse a local immutable whisper.cpp checkout during development:

```sh
BREEZEDESK_WHISPER_CPP_SOURCE_DIR=/path/to/whisper.cpp ./scripts/build.sh
```

## Build on Windows

Use Visual Studio 2022 Build Tools, Windows SDK, Ninja, CMake, and Qt 6.8+:

```bat
scripts\build.bat
scripts\run-tests.bat
scripts\build-and-run.bat
```

## Manual CMake build

```sh
cmake -S . -B build/manual -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel
ctest --test-dir build/manual --output-on-failure
```

`BREEZEDESK_ENABLE_WHISPER=OFF` builds protocol/domain tests without the native runtime. Production
packages always enable it. Windows packages use separate Vulkan and CUDA worker builds because GGML
backends are configure-time choices.

## Models and licensing

Models are never bundled in the repository or installer. The Models page downloads the recommended
Breeze-ASR-25 Q5, the higher-memory Q8, and Silero VAD from immutable source revisions and checks the
exact size and SHA-256 before use. Model license and provenance remain visible in the app.

BreezeDesk is MIT licensed. Qt is dynamically deployed under LGPL terms; whisper.cpp is MIT;
packaged FFmpeg is built without GPL/nonfree components. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Release packages

Unsigned local packages use the same deploy scripts as CI:

```sh
export BREEZEDESK_FFMPEG_DIR="$(packaging/macos/build-ffmpeg-lgpl.sh)"
packaging/macos/package.sh
```

```powershell
$env:BREEZEDESK_FFMPEG_DIR = packaging/windows/build-ffmpeg-lgpl.ps1 | Select-Object -Last 1
cmd /c packaging\windows\package.bat Universal --msix
cmd /c packaging\windows\package.bat CUDA
```

Signing is environment-controlled and optional for local builds. See
[release-packaging.md](docs/developer/release-packaging.md) for credentials, notarization, updater feeds,
and the exact output names.

Developer documentation starts at [architecture.md](docs/developer/architecture.md). User guidance
starts at [getting-started.md](docs/user/getting-started.md). Traditional Chinese documentation is in
[README.zh-TW.md](README.zh-TW.md).

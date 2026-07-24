<p align="center">
  <img src="resources/icons/breezedesk.png" alt="BreezeDesk logo" width="128">
</p>

<h1 align="center">BreezeDesk</h1>

<p align="center"><b>Private, offline transcription for long recordings — audio and transcripts never
leave your computer.</b></p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-2e7d32" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/platform-macOS%2014%2B%20%7C%20Windows%2010%2F11-4c7fb0" alt="Platform: macOS 14+ | Windows 10/11">
  <img src="https://img.shields.io/badge/Qt-6.8%2B-41cd52" alt="Qt 6.8+">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599c" alt="C++17">
</p>

<p align="center"><b>English</b> | <a href="README.zh-TW.md">繁體中文</a></p>

---

## 🚀 What is BreezeDesk?

BreezeDesk is a private, offline desktop workspace for long-form transcription. It imports audio and
video, records from a microphone, normalizes media with an LGPL FFmpeg sidecar, and transcribes
through a separate native Qt worker that links directly to the whisper.cpp C API — no `whisper-cli`,
no cloud ASR, no Python runtime, no telemetry. Audio and transcripts remain on the computer; the only
network features are verified model downloads and optional update checks. Starting a transcription
automatically downloads the pinned Silero VAD model when that required built-in model is missing or invalid.

The first release targets Taiwan Mandarin and mixed Chinese/English engineering meetings, on
macOS 14+ (Apple Silicon) and Windows 10 22H2+/Windows 11 (x64).

## ✨ Highlights

- 🛡️ **Crash-isolated engine** — transcription runs in a separate `breezedesk-asr-worker` process, so
  a native crash can never take down the app or your library.
- ⏳ **Built for hours-long recordings** — VAD-aware chunking, partial segments persisted as they
  arrive, cancellation, retry, resume for interrupted jobs, and deterministic overlap handling.
- 🗂️ **A real transcript library** — SQLite storage with revisions and editing, tags, search, trash,
  glossary profiles, prompt budgeting, and auditable alias replacement.
- 🎨 **Polished QML interface** — semantic design tokens, System/Light/Dark themes, English and
  Traditional Chinese, keyboard navigation, synchronized waveform and player, accessible controls.
- 🧠 **Verified model management** — Q5/Q8/custom GGML models with pause/resume downloads checked
  against the exact byte size and SHA-256 before use.
- ⌨️ **Scriptable CLI** — `breezedesk-cli` shares the GUI's database, models, worker, and exporters,
  with stable exit codes and `--json` output for automation.
- 📤 **Six export formats** — TXT, Markdown, SRT, VTT, JSON, and CSV, written atomically so a failed
  export never destroys a previously valid file.
- 📦 **Native distribution** — macOS and Windows packages with optional Sparkle/WinSparkle update
  checks.

## 🎯 Who is it for?

- Teams in Taiwan running Mandarin meetings that freely mix in English engineering and product terms.
- Anyone transcribing interviews, lectures, podcasts, or multi-hour recordings without uploading them.
- Environments where recordings are confidential and must never reach a cloud service.

## 💻 Under the hood

| Layer | Implementation |
| --- | --- |
| UI | Qt 6.8+ Quick/QML (Qt 6.10.1 release baseline) over a C++17 core |
| ASR | whisper.cpp pinned to an immutable commit, linked as a library by the worker process |
| Acceleration | Metal + Accelerate on macOS; Vulkan or CPU worker builds on Windows |
| Media | LGPL FFmpeg sidecar for probing and normalization; Qt Multimedia for capture and playback |
| Storage | SQLite for the library, jobs, chunk checkpoints, revisions, glossary, and audit data |
| Updates | Optional Sparkle (macOS) and WinSparkle (Windows) update checks |

## 🧠 Models

Models are never bundled in the repository or installer. The Models page (or `breezedesk-cli models
download`) fetches them from immutable source revisions, supports pause/resume, and verifies the
exact byte size and SHA-256 before a file can reach the worker. License and provenance stay visible
in the app, and a local whisper.cpp GGML `.bin` can be imported as a custom model. A queued
transcription that requires Silero VAD automatically uses this same verified download path and resumes
after the model is installed.

| Model | Quantization | Size | Purpose |
| --- | --- | ---: | --- |
| Breeze-ASR-25 Q5 (recommended) | Q5_K | ≈1.0 GB | Default for Apple Silicon and 8 GB machines |
| Breeze-ASR-25 Q8 | Q8_0 | ≈1.6 GB | Higher-quality option when more memory is available |
| Silero VAD 6.2.0 | F32 | <1 MB | Places long-form chunk boundaries in silence |

## 🛠 For developers

### Build on macOS

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

### Build on Windows

Use Visual Studio 2022 Build Tools, Windows SDK, Ninja, CMake, and Qt 6.8+:

```bat
scripts\build.bat
scripts\run-tests.bat
scripts\build-and-run.bat
```

The normal Windows build uses the CPU whisper.cpp worker, so it does not require an optional Vulkan
SDK. Always use `scripts\build-and-run.bat` for a development launch: raw CMake outputs are not
self-contained and can fail with missing Debug Qt DLLs such as `Qt6Networkd.dll`. The script finds the
matching Qt kit's `windeployqt.exe` and deploys the combined runtime required by the GUI, CLI, and ASR
worker. It also preloads `ffmpeg.exe` and `ffprobe.exe` next to the Debug executable. The first launch
reuses `BREEZEDESK_FFMPEG_DIR` or the source-built FFmpeg cache when available; otherwise it downloads
the checksum-pinned FFmpeg 8.1.2 source, portable w64devkit, and native Windows NASM, then builds the
same optimized offline LGPL sidecars used by packages. No MSYS2 installation or system-wide setup is
required, and later launches reuse the cached tools. To deploy Qt without launching or preparing media
tools, use:

```bat
scripts\deploy-debug.bat
```

For a nonstandard Qt layout, set `BREEZEDESK_WINDEPLOYQT` to the matching kit's `windeployqt.exe` before
running that script. Do not manually copy a subset of Qt DLLs from another build or Qt version.

The `debug-runtime-off` preset is only for protocol and UI tests; it deliberately cannot transcribe.
Do not distribute or use its executables for normal development.

### Manual CMake build

```sh
cmake -S . -B build/manual -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel
ctest --test-dir build/manual --output-on-failure
```

`BREEZEDESK_ENABLE_WHISPER=OFF` builds protocol/domain tests without the native runtime. Production
packages always enable it. Windows packages build separate Vulkan and CPU workers because GGML
backends are configure-time choices.

### Release packages

Unsigned local packages use the same deploy scripts as CI:

```sh
export BREEZEDESK_FFMPEG_DIR="$(packaging/macos/build-ffmpeg-lgpl.sh)"
packaging/macos/package.sh
```

```powershell
$env:BREEZEDESK_FFMPEG_DIR = packaging/windows/build-ffmpeg-lgpl.ps1 | Select-Object -Last 1
cmd /c packaging\windows\package.bat Universal --msix
```

Signing is environment-controlled and optional for local builds. See
[release-packaging.md](docs/developer/release-packaging.md) for credentials, notarization, updater
feeds, and the exact output names.

## 📖 Documentation

- **User guide** — start at [getting started](docs/user/getting-started.md), then
  [importing](docs/user/importing-media.md), [recording](docs/user/recording.md),
  [transcription](docs/user/transcription.md), [editing](docs/user/editing.md),
  [glossary](docs/user/glossary.md), [exporting](docs/user/exporting.md),
  [models](docs/user/models.md), [privacy](docs/user/privacy.md),
  [troubleshooting](docs/user/troubleshooting.md), and the [CLI](docs/user/cli.md).
- **Developer docs** — start at [architecture](docs/developer/architecture.md); build, testing,
  database, worker-protocol, and packaging guides live in [docs/developer](docs/developer), with
  design records in [docs/adr](docs/adr).
- **Project** — [CHANGELOG](CHANGELOG.md), [CONTRIBUTING](CONTRIBUTING.md),
  [SECURITY](SECURITY.md).

## 📄 License

BreezeDesk is [MIT licensed](LICENSE). Qt is dynamically deployed under LGPL terms, whisper.cpp is
MIT, Breeze-ASR-25 weights are Apache-2.0, and packaged FFmpeg is built without GPL/nonfree
components. Full attribution lives in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

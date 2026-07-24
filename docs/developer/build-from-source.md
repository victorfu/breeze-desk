# Build from source

Requirements are CMake 3.24+, Ninja, a C++17 compiler, Qt 6.8+ with Quick, SQL, Multimedia, Network,
Concurrent, SVG, Widgets, and Test, plus platform SDKs. Release CI uses Qt 6.10.1. whisper.cpp is fetched
at commit `f049fff95a089aa9969deb009cdd4892b3e74916`; set `BREEZEDESK_WHISPER_CPP_SOURCE_DIR` to an exact
local checkout to build without FetchContent network access.

macOS requires Xcode Command Line Tools and builds arm64/Metal+CPU for the supported release. Windows
source builds default to a CPU worker and require Visual Studio 2022 Build Tools and the Windows SDK;
the Vulkan worker requires the Vulkan SDK. Qt must be a dynamic LGPL-compatible
installation.

## Preset build

```sh
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

The equivalent repository scripts are `scripts/build.sh`, `scripts/run-tests.sh`, and
`scripts/build-and-run.sh`; Windows uses the matching `.bat` files. Debug intentionally uses a distinct
display name, executable, bundle id, and data directory so it cannot open Release settings by accident.
The normal `debug` and `release` presets explicitly enable whisper.cpp, so reconfiguring an older build
tree cannot silently retain a runtime-off cache value. On Windows, raw CMake outputs are not standalone;
`build-and-run.bat` uses the matching Qt kit to deploy the complete union of Debug runtime dependencies
for the application, CLI, and worker. `deploy-debug.bat` performs the same deployment without launching.

Important cache variables are:

| Variable | Default | Purpose |
| --- | --- | --- |
| `BREEZEDESK_ENABLE_WHISPER` | `ON` | Build/link the native ASR worker. |
| `BREEZEDESK_WHISPER_CPP_REF` | pinned commit | Immutable whisper.cpp revision; do not point at a branch. |
| `BREEZEDESK_WHISPER_CPP_SOURCE_DIR` | empty | Exact local checkout used instead of FetchContent. |
| `BREEZEDESK_WINDOWS_BACKEND` | `CPU` | `VULKAN` or `CPU` worker build. Release packaging selects its backend explicitly. |
| `BREEZEDESK_BUILD_TESTS` | `ON` | Configure Qt Test targets. |
| `BREEZEDESK_ENABLE_MODEL_INTEGRATION_TESTS` | `OFF` | Enable checksum-pinned tiny-model integration. |
| `BREEZEDESK_ENABLE_UPDATES` | `OFF` | Compile native updater adapters for a configured package. |
| `BREEZEDESK_WARNINGS_AS_ERRORS` | `ON` | Strict warnings for BreezeDesk-owned targets only. |

`CMakeLists.txt` centralizes product name, executable names, bundle ids, Windows product id, and Release/
Debug data-directory names. `project(... VERSION ...)` generates `version.h` and is the sole application
version source.

## Manual and reduced-runtime builds

To configure explicitly:

```sh
cmake -S . -B build/manual -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=/absolute/path/to/whisper.cpp
cmake --build build/manual --parallel
ctest --test-dir build/manual --output-on-failure
```

The isolated `debug-runtime-off` preset is useful for protocol/domain checks that intentionally omit the
native runtime:

```sh
cmake --preset debug-runtime-off
cmake --build --preset debug-runtime-off --parallel
ctest --preset debug-runtime-off
```

It uses `build/debug-runtime-off`, never the normal Debug cache, and is not a production package. The
equivalent manual switch remains `-DBREEZEDESK_ENABLE_WHISPER=OFF`. With whisper enabled, Windows places
the application, worker, and CLI in the chosen build-tree root. Other Ninja builds keep their target
subdirectories. On macOS the application is a bundle and the post-build rule copies the worker into
`Contents/MacOS`.

`BREEZEDESK_WINDOWS_BACKEND` is `VULKAN` or `CPU` and must use a separate build tree. The normal
Debug and Release source presets use CPU so transcription works without an optional GPU SDK; the
`windows-universal` preset opts into Vulkan. ccache is selected before
sccache. Compile commands are exported, Unity builds are disabled, project targets use strict warnings as
errors, and third-party targets do not inherit that policy.

macOS builds default `CMAKE_OSX_DEPLOYMENT_TARGET` to 14.0 before compiler initialization. Release
sidecars use the same default through `BREEZEDESK_MACOS_DEPLOYMENT_TARGET`; set that environment variable
before building FFmpeg and packaging only when intentionally changing the supported OS floor.

Development may find FFmpeg on PATH. Packages accept only `BREEZEDESK_FFMPEG_DIR` built without GPL or
nonfree options. Never distribute a Homebrew or third-party binary without checking its `-buildconf`.

The build enables `compile_commands.json`, disables Unity builds, and auto-selects ccache before sccache.
Keep cache directories inside the workspace in restricted environments. Third-party whisper/GGML targets
do not inherit BreezeDesk's warnings-as-errors policy.

Release package commands are:

```sh
export BREEZEDESK_FFMPEG_DIR="$(packaging/macos/build-ffmpeg-lgpl.sh)"
packaging/macos/package.sh
```

```powershell
$env:BREEZEDESK_FFMPEG_DIR = packaging/windows/build-ffmpeg-lgpl.ps1 | Select-Object -Last 1
cmd /c packaging\windows\package.bat Universal
```

See [release-packaging.md](release-packaging.md) for unsigned versus signed builds, MSIX, updater
dependencies, notarization, output names, and required CI variables.

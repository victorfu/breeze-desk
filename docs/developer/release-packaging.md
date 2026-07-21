# Release packaging

The top-level `project(BreezeDesk VERSION …)` declaration is the only product version source. Packaging
scripts invoke `cmake/ReadProjectVersion.cmake`; installer templates, filenames, release tag validation,
the generated C++ header, and bundle metadata must never carry a separately maintained version.

## Pinned release inputs

Release tooling verifies every downloaded archive before use:

- whisper.cpp commit `f049fff95a089aa9969deb009cdd4892b3e74916`;
- FFmpeg 8.1.2 source archive SHA-256
  `464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c`;
- Sparkle 2.9.2 revision `6276ba2b404829d139c45ff98427cf90e2efc59b`, release archive SHA-256
  `1cb340cbbef04c6c0d162078610c25e2221031d794a3449d89f2f56f4df77c95`;
- WinSparkle 0.9.3 revision `8ca58d903779b866eb9ed4628b0a36e4d488b623`, binary archive SHA-256
  `745985f41d2ab26b2d5a1cf87d76e4ed851039db19038e50610eb25ea0b73772`;
- vendored Lucide 1.16.0 icons at commit `2214caa407f4147449c81ac27e30d36edfb7b40f`, source
  archive SHA-256 `b831bb343805685d2afefb19aa30ee1cbaf2972c1af75ab501f58fbe01b77183`.

Hosted Windows CI uses Vulkan SDK 1.4.341. CUDA is intentionally supplied by the dedicated runner rather
than silently downloading a different Toolkit during a release.

Model files are never placed in an installer. FFmpeg is source-built with GPL, nonfree, network, and
autodetection disabled. Its exact `-buildconf` output and source record are included with each package.

## macOS arm64

Create the pinned sidecars and an unsigned local DMG:

```sh
export BREEZEDESK_FFMPEG_DIR="$(packaging/macos/build-ffmpeg-lgpl.sh)"
packaging/macos/package.sh
```

The script requires an Apple Silicon host and ImageMagick, rebuilds the checked-in macOS bundle icon
from the canonical 1024 px PNG as a Retina `.icns`, deploys dynamic Qt frameworks/QML imports/SQLite,
embeds the native Metal+CPU worker plus
`ffmpeg` and `ffprobe`, records linked libraries, and verifies every required executable has an arm64
slice. CMake and the source-built FFmpeg sidecars default to a macOS 14.0 deployment target. Set
`BREEZEDESK_MACOS_DEPLOYMENT_TARGET` before both commands only when intentionally changing that floor;
packaging inspects every Mach-O file and rejects any dependency that requires a newer macOS version.
The DMG contains an Applications shortcut and is verified with `hdiutil verify`.

For a signed direct-download package with updates:

```sh
export BREEZEDESK_SPARKLE_FRAMEWORK_DIR="$(packaging/macos/fetch-sparkle.sh)"
export BREEZEDESK_PACKAGE_UPDATES=1
export BREEZEDESK_APPCAST_URL=https://example.invalid/updates/appcast-macos.xml
export BREEZEDESK_EDDSA_PUBLIC_KEY='base64-public-key'
export BREEZEDESK_CODESIGN_IDENTITY='Developer ID Application: Example (TEAMID)'
export BREEZEDESK_NOTARY_PROFILE=breezedesk-notary
packaging/macos/package.sh
```

The identity signs nested Mach-O files, Sparkle helpers, the app, and the DMG with hardened runtime.
Notarization uses an existing `notarytool` keychain profile, then staples, validates, and performs a
Gatekeeper assessment. Sign the final update archive with Sparkle's pinned `sign_update` tool:

```sh
export BREEZEDESK_SPARKLE_SIGN_UPDATE="$BREEZEDESK_SPARKLE_FRAMEWORK_DIR/bin/sign_update"
export BREEZEDESK_SPARKLE_PRIVATE_KEY='private-key-content'
packaging/macos/sign-sparkle-update.sh dist/BreezeDesk-<version>-macOS-arm64.dmg
```

The private EdDSA key is sent to the tool through standard input and is not written by the script.
Output is `dist/BreezeDesk-<version>-macOS-arm64.dmg` plus `.sha256` and, for release CI,
`.edSignature`.

## Windows x64

Run from a Visual Studio 2022 developer command prompt with Qt, Ninja, NSIS, ImageMagick, Windows SDK,
MSYS2 (`make`, `nasm`, `diffutils`, and the mingw-w64 x64 GCC toolchain), and an LGPL FFmpeg directory
available. The package script
renders the canonical and tray-sized repository PNGs to a multi-resolution ICO before CMake
configures the executable resource and NSIS branding:

```bat
set BREEZEDESK_FFMPEG_DIR=C:\path\to\ffmpeg\bin
packaging\windows\package.bat Universal
packaging\windows\package.bat CUDA
```

Universal configures separate Vulkan and CPU whisper.cpp build trees. CUDA configures separate CUDA and
CPU trees. The installed layout preserves an unqualified preferred worker for compatibility and explicit
variants for runtime selection:

```text
bin/BreezeDesk.exe
bin/breezedesk-cli.exe
bin/breezedesk-asr-worker.exe
bin/workers/breezedesk-asr-worker-vulkan.exe   (Universal)
bin/workers/breezedesk-asr-worker-cuda.exe     (CUDA)
bin/workers/breezedesk-asr-worker-cpu.exe      (both)
bin/ffmpeg.exe
bin/ffprobe.exe
```

For CUDA, `deploy-cuda-runtime.ps1` inspects the built worker with `dumpbin`, copies only the CUDA runtime
DLLs it actually imports, records their SHA-256 values, and includes the Toolkit EULA. Qt, WinSparkle, and
NVIDIA DLL signatures are preserved; the signing hook signs BreezeDesk/FFmpeg executables and the final
installers rather than rewriting third-party DLL signatures.

Generate the Universal MSIX by passing `--msix`; ImageMagick builds the scale-, target-size-, and
theme-qualified assets from the repository PNGs, then Windows SDK `makepri` indexes them and
`makeappx` packages them:

```bat
packaging\windows\package.bat Universal --msix
```

MSIX identity values are supplied through `BREEZEDESK_MSIX_IDENTITY_NAME`,
`BREEZEDESK_MSIX_PUBLISHER`, and `BREEZEDESK_MSIX_PUBLISHER_DISPLAY_NAME`. The publisher must match the
certificate subject. An unsigned local MSIX can be created but cannot be installed without a trusted
development or production signature.

Set `BREEZEDESK_SIGNTOOL_CERT` to a PFX path and `BREEZEDESK_SIGNTOOL_PASSWORD` to sign staged EXEs,
the NSIS installer, and the MSIX. `BREEZEDESK_SIGNTOOL_SHA1` selects an already installed certificate
instead. Signing uses SHA-256, an RFC 3161 timestamp, and immediate `signtool verify /pa` validation.
Unsigned local NSIS builds remain supported when neither variable is present.

For direct-download updates, obtain the pinned runtime and enable the configured feed:

```powershell
$env:BREEZEDESK_WINSPARKLE_DIR = packaging/windows/fetch-winsparkle.ps1 | Select-Object -Last 1
$env:BREEZEDESK_PACKAGE_UPDATES = '1'
$env:BREEZEDESK_APPCAST_URL = 'https://example.invalid/updates/appcast-windows-universal.xml'
$env:BREEZEDESK_EDDSA_PUBLIC_KEY = 'base64-public-key'
packaging\windows\package.bat Universal
```

The per-user NSIS installer records its install location under `HKCU\Software\BreezeDesk`; the app uses
that marker to classify the LocalAppData installation as direct-download and enable WinSparkle. An MSIX
path under `WindowsApps` always takes precedence and keeps the native updater disabled.

After Authenticode signing, use the pinned companion tool to generate the enclosure signature. The
private key exists only in a temporary file for the duration of the signing process:

```powershell
$env:BREEZEDESK_WINSPARKLE_TOOL = "$env:BREEZEDESK_WINSPARKLE_DIR\winsparkle-tool.exe"
$env:BREEZEDESK_WINSPARKLE_PRIVATE_KEY = Get-Content .\private.key -Raw
packaging/windows/sign-winsparkle-update.ps1 dist/BreezeDesk-<version>-Windows-x64-Universal-Setup.exe
```

Outputs are:

- `dist/BreezeDesk-<version>-Windows-x64-Universal-Setup.exe`;
- `dist/BreezeDesk-<version>-Windows-x64-CUDA-Setup.exe`;
- `dist/BreezeDesk-<version>-Windows-x64.msix` when requested;
- SHA-256 sidecars for every generated installer.

The CUDA package requires a CUDA-capable build machine and Toolkit; ordinary hosted CI does not claim to
test CUDA initialization or inference.

## Tag release workflow

Tags must exactly equal `v<CMake-project-version>`, and the matching `CHANGELOG.md` section must exist.
The release workflow fails with the missing variable names before doing expensive builds. Configure:

- macOS secrets: `MACOS_CERTIFICATE_P12_BASE64`, `MACOS_CERTIFICATE_PASSWORD`,
  `MACOS_CODESIGN_IDENTITY`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`,
  `SPARKLE_PRIVATE_KEY`;
- Windows secrets: `WINDOWS_CERTIFICATE_PFX_BASE64`, `WINDOWS_CERTIFICATE_PASSWORD`,
  `WINSPARKLE_PRIVATE_KEY`;
- repository variables: `SPARKLE_PUBLIC_KEY`, `WINSPARKLE_PUBLIC_KEY`,
  `BREEZEDESK_UPDATE_FEED_BASE_URL`, `WINDOWS_MSIX_PUBLISHER`;
- optional variable `RUN_WINDOWS_CUDA_PACKAGE=true` and a self-hosted `Windows`, `X64`, `CUDA` runner.

`BREEZEDESK_UPDATE_FEED_BASE_URL` is a stable HTTPS directory such as the GitHub
`releases/latest/download` URL and must not end in `/`. Release publication creates separate
`appcast-macos.xml`, `appcast-windows-universal.xml`, and (when built) `appcast-windows-cuda.xml`, plus a
machine-readable release manifest and aggregate checksums. Sparkle update signatures are mandatory for
every DMG or NSIS enclosure. No credential, private key, or certificate is committed or uploaded as an
artifact.

The workflows validate build and package mechanics, not backend performance. Metal is exercised by the
optional tiny-model nightly test. Vulkan and CPU are built in hosted Windows CI. CUDA and the full Breeze
model require explicitly provisioned runners and are reported as untested when those runners are absent.

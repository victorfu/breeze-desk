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
- vendored Lucide 1.16.0 icons at commit `2214caa407f4147449c81ac27e30d36edfb7b40f`, source
  archive SHA-256 `b831bb343805685d2afefb19aa30ee1cbaf2972c1af75ab501f58fbe01b77183`.

Hosted Windows CI uses Vulkan SDK 1.4.341.

Model files are never placed in an installer. FFmpeg is source-built with GPL, nonfree, network, and
autodetection disabled. Its exact `-buildconf` output and source record are included with each package.

## macOS arm64

Create the pinned sidecars and an unsigned local DMG:

```sh
export BREEZEDESK_FFMPEG_DIR="$(packaging/macos/build-ffmpeg-lgpl.sh)"
packaging/macos/package.sh
```

The script requires an Apple Silicon host and ImageMagick, rebuilds the checked-in macOS bundle icon
from `resources/icons/breezedesk-macos.png` as a Retina `.icns`, deploys dynamic Qt frameworks/QML imports/SQLite,
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

The identity is never auto-detected. `BREEZEDESK_CODESIGN_IDENTITY` must contain the exact Developer ID
Application identity already available in the keychain. The identity signs nested Mach-O files,
Sparkle helpers, the app, and the DMG with hardened runtime.
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

The Microsoft Store MSIX is the only Windows distribution. Run from a Visual Studio 2022 developer
command prompt with Qt, Ninja, ImageMagick, Windows SDK, Vulkan SDK, and an LGPL FFmpeg directory
available. `build-ffmpeg-lgpl.ps1` bootstraps checksum-pinned portable w64devkit and native Windows NASM
archives when that directory must be built, so MSYS2 is not required.

The public Partner Center values from **Product identity** are committed in
`packaging/windows/msix-identity.ps1`, so both local and CI release builds use the official Store
identity by default:

```powershell
.\scripts\package-windows.ps1
```

The wrapper builds or reuses the pinned LGPL FFmpeg sidecars, then delegates to
`packaging/windows/package.bat`. An existing `BREEZEDESK_FFMPEG_DIR` remains available as an override.
It discovers Qt from `BREEZEDESK_QT_ROOT`, `QT_PATH`, `Qt6_DIR`, `CMAKE_PREFIX_PATH`, `PATH`, or a
standard `C:\Qt\<version>\msvc*_64` installation. ImageMagick is resolved from `PATH`,
`BREEZEDESK_MAGICK`, or its standard Program Files installation. The Vulkan SDK is resolved from
`VULKAN_SDK`, `VK_SDK_PATH`, or `C:\VulkanSDK\<version>` and must provide headers, `vulkan-1.lib`, and
`glslc.exe`.

For a development package only, `BREEZEDESK_MSIX_IDENTITY_NAME`, `BREEZEDESK_MSIX_PUBLISHER`, and
`BREEZEDESK_MSIX_PUBLISHER_DISPLAY_NAME` may override the committed values when all three are set
together. Never use an overridden identity for a Store update.

The script configures separate Vulkan and CPU whisper.cpp build trees. The staged layout preserves an
unqualified preferred worker for compatibility and explicit variants for runtime selection:

```text
bin/BreezeDesk.exe
bin/breezedesk-cli.exe
bin/breezedesk-asr-worker.exe
bin/workers/breezedesk-asr-worker-vulkan.exe
bin/workers/breezedesk-asr-worker-cpu.exe
bin/ffmpeg.exe
bin/ffprobe.exe
```

ImageMagick builds the scale-, target-size-, and theme-qualified assets from the repository PNGs,
Windows SDK `makepri` indexes them, and `makeappx` creates the unsigned Store submission artifact.
Outputs are `dist/BreezeDesk-<version>-Windows-x64.msix` and its `.sha256` sidecar.

The Microsoft Store signs the package after certification. CI therefore does not use or retain a
Windows code-signing certificate, and the unsigned MSIX is never published as a GitHub Release asset.
Download the `windows-msix` workflow artifact and upload it manually in Partner Center.

An unsigned MSIX cannot be installed locally. Create a separately named development copy and sign only
that copy with a self-signed certificate whose subject matches the manifest publisher:

```powershell
.\scripts\package-windows-dev.ps1
# Reuse the existing verified Store package instead of rebuilding it:
.\scripts\package-windows-dev.ps1 -ReuseStorePackage
```

The script verifies the Store artifact against its checksum before copying it, signs only
`BreezeDesk-<version>-Windows-x64-Development.msix`, writes a separate checksum, and verifies that the
unsigned Store MSIX did not change. To trust the exported public certificate in
`LocalMachine\TrustedPeople` and install the development copy, run this from an elevated PowerShell
window:

```powershell
.\scripts\package-windows-dev.ps1 -ReuseStorePackage -Install
```

The development certificate and `-Development.msix` are for controlled test machines only. Neither is
uploaded to Partner Center, and the original Store MSIX remains unsigned.

## Tag release workflow

Tags must exactly equal `v<CMake-project-version>`, and the matching `CHANGELOG.md` section must exist.
The release workflow fails with the missing variable names before doing expensive builds. Configure:

- macOS secrets: `APPLE_CERTIFICATE`, `APPLE_CERTIFICATE_PASSWORD`,
  `MACOS_CODESIGN_IDENTITY`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`,
  `SPARKLE_PRIVATE_KEY`;
- repository variables for macOS: `SPARKLE_PUBLIC_KEY`, `BREEZEDESK_UPDATE_FEED_BASE_URL`.

The workflow imports the certificate and maps the `MACOS_CODESIGN_IDENTITY` secret to the
`BREEZEDESK_CODESIGN_IDENTITY` environment variable used by the packaging script. It does not scan the
keychain to choose an identity automatically.

Windows needs no CI secret or repository variable: its public Store identity is versioned with the
packaging source, while Store certification supplies the public distribution signature.

`BREEZEDESK_UPDATE_FEED_BASE_URL` is a stable HTTPS directory such as the GitHub
`releases/latest/download` URL and must not end in `/`. GitHub Release publication contains the signed,
notarized DMG, `appcast-macos.xml`, a machine-readable release manifest, and aggregate checksums. The
Windows MSIX remains a workflow artifact for manual Store submission. No credential, private key, or
certificate is committed or uploaded as an artifact.

The workflows validate build and package mechanics, not backend performance. Metal is exercised by the
optional tiny-model nightly test. Vulkan and CPU are built in hosted Windows CI. The full Breeze
model requires an explicitly provisioned runner and is reported as untested when that runner is absent.

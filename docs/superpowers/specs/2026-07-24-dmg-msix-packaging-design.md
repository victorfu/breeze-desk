# Shipping design: macOS DMG and Windows Store MSIX

Status: Proposed — 2026-07-24

## Goal

Make the existing packaging pipeline produce releasable artifacts on the two channels this project
will actually ship on:

- macOS: a signed, notarized DMG with Sparkle automatic updates, distributed from GitHub Releases;
- Windows: an unsigned MSIX uploaded by hand to Microsoft Partner Center, updated by the Store.

Linux is out of scope. No third channel is introduced.

## Current state

Packaging code is complete and hardened, but has never run: the repository has no tags, and neither
`ci.yml` nor `nightly.yml` exercises a packaging path. The gap is credentials and channel decisions,
not implementation.

The existing Windows pipeline targets a channel this project will not use. It builds an NSIS installer,
embeds WinSparkle, and requires Authenticode signing secrets that do not exist. `release.yml` fails
before building when those secrets are absent.

## Decisions

### D1. Windows ships MSIX only

The NSIS installer, the direct-download channel, and WinSparkle are removed. The Store is the sole
Windows distribution channel and supplies updates.

This makes Authenticode signing unnecessary: Partner Center re-signs uploaded packages with a Microsoft
certificate. The unsigned MSIX produced by CI is an input to manual submission, not a user-facing
download.

### D2. The CUDA package variant goes with the NSIS channel

This follows from D1 rather than deciding anything. The `windows-cuda` job packages an NSIS installer;
removing that channel leaves the job calling a script that no longer exists. It is deleted because it
would otherwise be broken, not because CUDA support is being withdrawn.

Nothing is lost, because nothing was running. The job is gated on `RUN_WINDOWS_CUDA_PACKAGE == 'true'`
and the repository has no variables set, no self-hosted runner registered, no secrets, and no releases.
Neither `ci.yml` nor `nightly.yml` compiles a CUDA target. The path has never been built or shipped.

Windows release packaging keeps only the Universal variant — Vulkan preferred worker plus CPU fallback —
and Vulkan covers NVIDIA hardware. `BREEZEDESK_WINDOWS_BACKEND=CUDA` remains a supported source build.

The app's CUDA wiring — the `BackendPreference::Cuda` enum, `WorkerRegistry`'s `-cuda.exe` probe and
fallback order, and the `WhisperBackendInfo` compile-time branch — is untouched. Shipped MSIX packages
will never contain a `-cuda.exe`, so `WorkerRegistry` falls back exactly as its existing tests describe.

Out of scope but adjacent: the settings backend picker offers every backend on every platform, so this
change does not create a dead CUDA option on Windows — one already exists, alongside Metal on Windows and
CUDA on macOS. Tracked separately.

### D3. macOS keeps the direct-download channel

Gatekeeper offers no unsigned option on current macOS, so Developer ID signing and notarization are
mandatory, not optional hardening. Sparkle remains the update mechanism.

### D4. Appcast is a GitHub Release asset

`generate-release-metadata.py` already emits appcast XML into the release assets, and CHANGELOG content
is embedded as the item `<description>`. The feed URL is
`https://github.com/victorfu/breeze-desk/releases/latest/download/appcast-macos.xml`, which resolves to
the newest release.

No `pages` branch, no appcast deploy job, and no separately rendered release-notes site.

### D5. MSIX is not published to GitHub Releases

An unsigned MSIX cannot be installed by an end user. Publishing it beside the DMG would offer a download
that fails. CI uploads it as a workflow artifact for manual Partner Center submission.

GitHub Releases therefore carry: the DMG, its `.sha256`, its `.edSignature`, and `appcast-macos.xml`.

### D6. Update keys are new, not shared with snap-tray

BreezeDesk gets its own Sparkle EdDSA key pair so a key compromise in one project cannot sign updates
for the other. Only a macOS pair is needed; WinSparkle keys are not generated.

### D7. The codesign identity is discovered, not configured

The identity is read from the imported keychain with
`security find-identity -v -p codesigning | grep "Developer ID Application"`, removing the
`MACOS_CODESIGN_IDENTITY` secret. Certificate import keeps using `apple-actions/import-codesign-certs`.

## Changes

### macOS

**New `packaging/macos/entitlements.plist`.** The app is signed with the hardened runtime but currently
has no entitlements, because every `codesign` call passes `--preserve-metadata=entitlements,requirements`
and nothing supplies them initially. Required entries:

| Entitlement | Reason |
| --- | --- |
| `com.apple.security.device.audio-input` | recording; the hardened runtime denies microphone access without it |
| `com.apple.security.cs.disable-library-validation` | dynamically deployed Qt frameworks and Sparkle |
| `com.apple.security.cs.allow-unsigned-executable-memory` | ggml/whisper.cpp code generation |
| `com.apple.security.cs.allow-jit` | QML engine |

`package.sh` signs the outer `.app` with `--entitlements`, and applies the same file to
`breezedesk-asr-worker`, which is the process that executes ggml kernels. The `ffmpeg` and `ffprobe`
sidecars keep the current entitlement-free signature.

Which process needs `audio-input` must be verified, not assumed. The design places it on the main bundle
because capture runs there through Qt Multimedia, but TCC binds the microphone prompt to the process that
opens the device. Recording is a required post-notarization verification step, not an inference.

**`create-dmg` replaces bare `hdiutil`,** falling back to `hdiutil` when unavailable so local unsigned
builds keep working without Homebrew.

### Windows

Deleted:

- `packaging/windows/installer.nsi`
- `packaging/windows/fetch-winsparkle.ps1`
- `packaging/windows/sign-winsparkle-update.ps1`
- `packaging/windows/deploy-cuda-runtime.ps1`
- `src/update/WinSparkleUpdateService_win.cpp`

`packaging/windows/package.bat`: the NSIS invocation, the WinSparkle staging block, the
`BREEZEDESK_PACKAGE_UPDATES` and `BREEZEDESK_WINSPARKLE_DIR` handling, and the `CUDA` variant are removed.
The script stages the Universal build and emits an MSIX. `--msix` stops being a flag because MSIX is the
only output.

`src/update/CMakeLists.txt`: the `elseif(WIN32)` branch is removed, so Windows links only
`NullUpdateService`. `CMakeLists.txt` forces `BREEZEDESK_ENABLE_UPDATES=OFF` on Windows and fails
configuration if it is switched on, preventing an undefined `createNativeUpdateService`.

`InstallSourceClassifier` is left intact. It still reports `"msix"`, which keeps
`UpdateCoordinator` on the null service, and its `"direct"` branch remains correct for source builds.

`packaging/windows/create-msix.ps1` keeps its identity variables but loses its development-only defaults
for the release path: CI must supply real values or fail, so a package is never built with a publisher
that Partner Center will reject.

**New `packaging/windows/create-dev-certificate.ps1`.** An unsigned MSIX cannot be installed, so there is
currently no way to test the package locally. This generates a self-signed certificate matching the
manifest publisher, signs a local MSIX, and prints the trust-store import command. It is a local tool and
is never used by CI.

### Release workflow

`release.yml`:

- `windows-universal` becomes `windows-msix`; the Authenticode secret validation and the
  `Decode Authenticode certificate` step are removed, along with `choco install nsis`;
- the `windows-cuda` job and the `RUN_WINDOWS_CUDA_PACKAGE` variable are removed;
- the MSIX identity variables are validated before building;
- `publish` requires exactly the macOS artifacts; the `artifacts >= 3` check becomes a DMG-present check;
- the MSIX is uploaded as a workflow artifact and excluded from the release.

`scripts/generate-release-metadata.py`: the `windows-x64-universal` and `windows-x64-cuda` appcast
mappings are removed, leaving `appcast-macos.xml`.

### Documentation

`docs/developer/release-packaging.md` is rewritten for the two surviving channels.

A new ADR records D1. Dropping the Windows direct-download channel changes how users receive the app and
which update mechanism is compiled in, which is the kind of decision the ADR log exists for. D2 needs no
ADR of its own: ADR 0005 describes the packaging of variants that were never built, so its consequences
section is amended in place to say the CUDA variant is not packaged.

## Credentials

Reusable from snap-tray, which already ships signed and notarized macOS builds:

| Name | Kind | Value |
| --- | --- | --- |
| `MACOS_CERTIFICATE_P12_BASE64` | secret | snap-tray's `APPLE_CERTIFICATE` |
| `MACOS_CERTIFICATE_PASSWORD` | secret | snap-tray's `APPLE_CERTIFICATE_PASSWORD` |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | secret | copied unchanged |
| `WINDOWS_MSIX_PUBLISHER` | variable | `CN=88BB68F4-693F-45D9-BF2F-3CF9C709619F`, a Partner Center account value shared by all of the account's apps |

New:

| Name | Kind | Source |
| --- | --- | --- |
| `SPARKLE_PRIVATE_KEY` | secret | Sparkle `generate_keys` |
| `SPARKLE_PUBLIC_KEY` | variable | the same key pair |
| `BREEZEDESK_UPDATE_FEED_BASE_URL` | variable | `https://github.com/victorfu/breeze-desk/releases/latest/download` |
| `BREEZEDESK_MSIX_IDENTITY_NAME` | variable | assigned by Partner Center after reserving the BreezeDesk name |
| `BREEZEDESK_MSIX_PUBLISHER_DISPLAY_NAME` | variable | Partner Center publisher display name |

Retired: `WINDOWS_CERTIFICATE_PFX_BASE64`, `WINDOWS_CERTIFICATE_PASSWORD`, `WINSPARKLE_PRIVATE_KEY`,
`WINSPARKLE_PUBLIC_KEY`, `MACOS_CODESIGN_IDENTITY`, `RUN_WINDOWS_CUDA_PACKAGE`.

`BREEZEDESK_MSIX_IDENTITY_NAME` is the only unobtainable value today. It blocks Store submission but not
the macOS release or MSIX build mechanics, which run against a development identity until it exists.

## Verification

Mechanical checks belong in CI. These require a human and a real device, and none may be reported as
passing without being run:

1. the notarized DMG mounts, installs, launches, and **records audio** — this validates D3's entitlements;
2. Sparkle detects an update from the published appcast and applies it across two real releases;
3. a locally dev-signed MSIX installs, launches, transcribes, and downloads a model — MSIX redirects
   `AppLocalDataLocation` writes into the package-private store, and the model download, SQLite database,
   `SingleInstanceGuard`, and the `ffmpeg`/worker child processes all depend on that path behaving;
4. Partner Center accepts the package identity and passes certification. `runFullTrust` requires a written
   justification during submission.

Item 3 is the largest unknown in this design. The MSIX path has never been installed, only built.

## Rejected alternatives

**Keeping the NSIS channel alongside the Store.** Two Windows channels means maintaining WinSparkle, an
appcast, and update-source classification for an audience that can use the Store. The user asked for one
channel; carrying a second unused one is cost without a consumer.

**Buying an Authenticode certificate.** Post-2023 CA/B rules put the private key on hardware or a cloud
signing service, so the `.pfx`-in-a-secret pattern the current workflow assumes no longer works for newly
issued certificates. Store distribution avoids the problem rather than paying to reintroduce it.

**Splitting MSIX into its own workflow, as snap-tray does.** BreezeDesk compiles whisper.cpp, FFmpeg, and
two worker build trees; a separate workflow repeats all of it to package an artifact the same job already
produced.

**Publishing the MSIX to GitHub Releases.** It cannot be installed without a signature, so it is a
download that only generates support questions.

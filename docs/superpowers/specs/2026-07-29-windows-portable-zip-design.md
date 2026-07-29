# Shipping design: Windows portable ZIP

Status: Implemented 2026-07-29. The next tagged release exercises the complete hosted CI publication
path.

## Goal

Give GitHub Release visitors a Windows artifact they can actually run. The published
`BreezeDesk-<version>-Windows-x64.msix` is an unsigned Store submission input and cannot be
installed, so today a Windows user landing on the Releases page has nothing usable. The portable
ZIP closes that gap: download, extract, run `bin\BreezeDesk.exe`. The Microsoft Store remains the
primary, certified, auto-updating channel.

## Current state

`packaging/windows/package.bat` already stages a complete runnable tree at
`build\package-windows-msix`: `bin\` holds the app, CLI, unqualified preferred worker,
`workers\breezedesk-asr-worker-{vulkan,cpu}.exe`, `ffmpeg.exe`, `ffprobe.exe`, and the windeployqt
output; `share\breezedesk\licenses\` holds every license and source record. The MSIX-only additions
(`AppxManifest.xml`, `Assets\`, `resources.pri`) are made to a separate layout copy inside
`create-msix.ps1`, so the stage directory is a clean portable payload as-is.

`release.yml` publishes only the MSIX and its checksum. `scripts/generate-release-metadata.py`
classifies artifacts by filename suffix and treats unknown names as `supporting-file`. Models are
never packaged; the app downloads them at runtime.

## Decisions

### D1. Portable means no-install, not portable data

Extract-and-run. Data stays at the platform application-data location (`%LOCALAPPDATA%`), exactly
as for any non-Store install; `StoragePaths::root()` is untouched and no application code changes.
Power users already have the `BREEZEDESK_DATA_ROOT` environment variable to relocate data. A true
portable-data mode (marker file next to the executable) is listed under future work and is out of
scope here.

### D2. The ZIP is produced from the existing stage, in the same job

A new `packaging/windows/create-portable-zip.ps1` runs from `package.bat` immediately after
`create-msix.ps1`. No second CI build (rejected: 30–40 wasted minutes), no unpacking the MSIX to
re-zip it (rejected: must strip manifest assets, easy to get wrong). A local
`scripts\package-windows.ps1` run therefore produces both artifacts, identical to CI.

### D3. Versioned root folder inside the archive

The archive holds exactly one root entry, `BreezeDesk-<version>-Windows-x64-portable\`, matching
the ZIP basename. Extracting a new version beside an old one can never mix file sets — windeployqt
output changes across Qt versions, and stale-DLL breakage is miserable to debug. Upgrades are
"extract the new folder, delete the old one"; data is unaffected because of D1.

### D4. No Windows update feed

No appcast and no auto-update for the portable channel; users replace the folder by hand. Windows
builds keep `BREEZEDESK_ENABLE_UPDATES=OFF`. The artifact still gets a `release-manifest.json`
entry (platform `windows-x64-portable`), which is a stable hook if an update checker is ever added.

### D5. SmartScreen is documented, not solved

The executables are unsigned, so the first launch shows "Windows protected your PC"
(More info → Run anyway), and the downloaded ZIP carries Mark of the Web. `README.txt` inside the
archive and the release body both explain this. Actually removing the warning requires a
code-signing certificate (e.g. Azure Trusted Signing) — future work, shared with the signed
sideload-MSIX idea.

## Deliverables

### 1. `packaging/windows/create-portable-zip.ps1` (new)

- Parameters: `-StageDirectory -OutputFile -Version -ProductName -ExecutableName`, all mandatory;
  `$ErrorActionPreference = "Stop"` and upfront validation, matching `create-msix.ps1` style.
- Validates: the stage directory exists, `bin\<ExecutableName>.exe` exists, `Version` is a
  three-part numeric version, and the `OutputFile` basename ends in `-Windows-x64-portable.zip`.
- Builds a fresh layout each run: delete and recreate `build\package-windows-portable\`, copy the
  stage into `build\package-windows-portable\<zip-basename-without-extension>\`.
- Renders `README.txt` into that folder from `packaging/windows/portable-readme.txt.in`, replacing
  `@PRODUCT_NAME@`, `@VERSION@`, `@EXECUTABLE_NAME@` (same token style as `AppxManifest.xml.in`).
- Creates the archive with `cmake -E tar cf <output> --format=zip <folder>` executed with the
  working directory set to `build\package-windows-portable`, so archive paths start at the
  versioned root folder. `cmake` is already a verified prerequisite of `package.bat`;
  `Compress-Archive` is avoided deliberately (slow, historical long-path issues).
- Prints the resolved output path. Checksum writing stays in `package.bat` for symmetry with the
  MSIX (single-purpose scripts).

### 2. `packaging/windows/portable-readme.txt.in` (new)

Plain ASCII text, short. Content:

- what this is: `@PRODUCT_NAME@ @VERSION@` portable build for Windows x64; run
  `bin\@EXECUTABLE_NAME@.exe`; no installation.
- SmartScreen: first launch may show "Windows protected your PC" — choose More info, then
  Run anyway. The build is unsigned; verify the download with the published `.sha256` file.
- data location: settings, database, and downloaded models live under `%LOCALAPPDATA%`, shown in
  Settings. The Microsoft Store build keeps its own separate data.
- models: the first transcription downloads the recommended model (about 1 GB, one time).
- upgrade: extract the new version's folder, delete the old folder; data is kept. The Microsoft
  Store build is preferred when available because it is certified and updates automatically.
- licenses: `share\breezedesk\licenses\`. Project: https://github.com/victorfu/breeze-desk

### 3. `packaging/windows/package.bat`

After the MSIX checksum step: set
`PORTABLE_ZIP=%PROJECT_ROOT%\dist\%BREEZEDESK_PRODUCT_NAME%-%VERSION%-Windows-x64-portable.zip`,
call `create-portable-zip.ps1` with the stage directory, version, product name, and executable
name, call `write-checksum.ps1` on the ZIP, and echo the ZIP path next to the MSIX path. Every failure exits
non-zero as elsewhere in the script.

### 4. `.github/workflows/release.yml`

- Rename the job display name to `Package Windows Store MSIX and portable ZIP` (job id stays
  `windows-msix`).
- Add a second upload step, artifact `windows-portable`, containing
  `dist/<name>-Windows-x64-portable.zip` and its `.sha256`.
- `publish` job:
  - add `-name '*.zip'` to the `find` that assembles `release-assets`;
  - add a count check mirroring the MSIX one: exactly one portable ZIP, else fail;
  - append the ZIP to the artifact list passed to `generate-release-metadata.py`;
  - rewrite the release body note. Keep the maintainer-only MSIX warning, and add a user-facing
    paragraph, e.g.:

    > **Windows users:** download `BreezeDesk-<version>-Windows-x64-portable.zip`, extract it, and
    > run `bin\BreezeDesk.exe`. No installation. The build is unsigned, so SmartScreen may warn on
    > first launch — choose "More info", then "Run anyway". Prefer the Microsoft Store build when
    > it is available.

### 5. `scripts/generate-release-metadata.py`

`platform_for()` gains one branch: names ending in `-Windows-x64-portable.zip` map to
`windows-x64-portable`. No feed generation for Windows (D4). `checksums.txt` and
`release-manifest.json` pick the ZIP up automatically once it is passed in.

### 6. Documentation

- `docs/developer/release-packaging.md`: the Windows section no longer claims the Store MSIX is
  the only distribution. Document the second artifact, the archive layout (versioned root folder,
  `README.txt`, no `AppxManifest.xml`/`Assets\`/`resources.pri`), and that
  `scripts\package-windows.ps1` now emits both outputs.
- `docs/user/getting-started.md`: add a short Windows install note covering the two channels,
  SmartScreen, the data location, and that Store and portable builds keep separate data (MSIX
  filesystem virtualization) — switching channels does not migrate library or settings.
- `CHANGELOG.md`: entry under the next release.

`scripts/package-windows-dev.ps1` and the development-certificate flow are untouched.

## Test plan (Windows machine)

Build and artifact checks:

1. `scripts\package-windows.ps1` completes; `dist\` contains the MSIX, the portable ZIP, and both
   `.sha256` sidecars.
2. The ZIP holds a single root folder named `BreezeDesk-<version>-Windows-x64-portable`;
   `README.txt` is at its root; `share\breezedesk\licenses\` is present;
   no `AppxManifest.xml`, `Assets\`, or `resources.pri` anywhere in the archive.
3. `certutil -hashfile` output matches the `.sha256` sidecar content.

Runtime checks (from an extracted copy, including one path containing a space or CJK characters):

4. `bin\BreezeDesk.exe` launches; import a short media file; the recommended model downloads; a
   transcription completes (watch Activity).
5. The Vulkan worker is used on a Vulkan-capable machine; the CPU fallback engages otherwise.
6. A settings change persists across relaunch; data and logs appear under `%LOCALAPPDATA%`.
7. `bin\breezedesk-cli.exe --help` runs and exits 0.
8. Launching a second instance behaves sanely (existing single-instance handling; observe, not a
   gate).

CI path: exercised by the next real tag release; optionally rehearsed first with a tag on a fork.

## Risks and notes

- SmartScreen and antivirus heuristics dislike unsigned executables; this is the accepted cost of
  the channel until code signing lands (future work).
- Store and portable builds on the same machine keep separate data stores; simultaneous use is not
  a supported workflow, only observed in testing.
- Archive size is estimated at 200–400 MB — far under GitHub's 2 GB asset limit, and ZIP64
  concerns start only near 4 GB.

## Future work

- True portable-data mode: a marker file next to the executable redirects `StoragePaths::root()`;
  needs tests and a guarantee the Store build never trips it.
- Code signing via Azure Trusted Signing for the portable executables, which would also enable a
  signed sideload MSIX with its own identity.

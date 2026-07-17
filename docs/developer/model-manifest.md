# Model manifest

`resources/models/models.json` is versioned, embedded in the app, and contains id, display metadata,
engine, architecture, quantization, immutable URL, filename, exact size, SHA-256, source repository and
revision, license, recommended memory, capabilities, language, and recommendation flag. Validation rejects
duplicate ids, unsafe filenames, invalid hashes, mutable/invalid source metadata, and a manifest without
exactly one recommended ASR model.

Downloads use redirects, Range resume, `.part`, bounded 1/2/4/8-second retries, disk-space checks, and
background SHA-256 verification. Only a size- and checksum-matched file is atomically renamed. A checksum
failure deletes the corrupt part. Installers contain the manifest but no model.

The embedded SHA-256 is also sent to the native worker for a second, just-in-time verification before
every model load. Silero VAD uses the same path. Imported custom models are hashed while their managed
copy is written, and the digest is committed atomically to a neighboring `.sha256` sidecar. Discovery
requires a valid sidecar; manual verification and the worker compare the current file against that
import-time digest. The GUI thread never hashes model weights.

## Schema version 1

| Field | Validation/use |
| --- | --- |
| `id` | Unique stable application id; used by settings/jobs and never localized. |
| `displayName`, `description` | User-visible catalog metadata. |
| `engine`, `architecture`, `quantization` | Compatibility and diagnostics metadata. |
| `downloadUrl` | Valid URL; release entries use HTTPS and an immutable revision rather than a branch. |
| `fileName` | Basename under the model directory; path separators are rejected. |
| `fileSize`, `sha256` | Positive exact byte count and 64 lowercase hexadecimal characters. |
| `sourceRepository`, `sourceRevision` | Provenance; release entries use a full 40-character immutable commit. |
| `licenseName`, `licenseUrl` | License shown on the model card and in notices. |
| `recommendedMemoryBytes` | UI guidance, not a hardware-performance guarantee. |
| `capabilities`, `defaultLanguage`, `isRecommended` | ASR/VAD selection and first-run guidance. |

There must be exactly one recommended entry. Duplicate ids, path separators, non-positive sizes, invalid
hashes, missing license fields, an unexpected engine, invalid URLs, and short source revisions make the
manifest unusable rather than silently downgrading integrity. Review additionally enforces HTTPS,
revision-qualified release URLs, and meaningful capability values.

## Download state machine

`ModelDownloadOperation` writes `<file>.part`, follows redirects, validates Range responses, and reports
progress/speed/ETA. Network retries use bounded 1, 2, 4, and 8 second backoff. Before transfer it checks
available disk space for the remaining bytes plus reserve. Pause preserves a valid partial file; cancel
removes it when requested.

Completion closes the network stream, verifies size, streams SHA-256 outside the UI thread, and atomically
renames only on a match. A mismatch deletes corrupt data and returns `ModelChecksumMismatch`. Model load
requests repeat the digest in protocol v2; the native worker hashes again immediately before calling
libwhisper.

## Updating manifest entries

When changing a built-in artifact:

1. identify an immutable upstream revision and verify its declared license;
2. download once outside ordinary CI and record exact byte size and SHA-256;
3. use a revision-qualified URL and update source/license metadata together;
4. run `tst_Models` manifest, download/resume, checksum, atomic-rename, and in-use deletion tests;
5. update `THIRD_PARTY_NOTICES.md` and user documentation if provenance or terms changed.

Never substitute a dummy digest, bundle a gigabyte model, or make ordinary PR CI retrieve Breeze Q5/Q8.
The pinned tiny integration model and full Breeze smoke policy are described in [testing.md](testing.md).

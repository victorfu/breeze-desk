# ADR 0002: Native whisper worker

Status: Accepted — 2026-07-17

## Context

whisper.cpp inference can consume substantial memory and GPU resources for hours. A native fault or
backend failure must not terminate the editor/database process, but model reload per chunk would waste
time. Cancellation must remain responsive even while `whisper_full` is running, and the product must
directly integrate libwhisper rather than execute `whisper-cli`.

## Decision

Link the pinned `whisper` target into a Qt Core helper executable and communicate over authenticated,
length-prefixed CBOR on `QLocalSocket`. The worker main thread owns IPC/heartbeat; one dedicated QThread
owns a RAII model session and serializes inference. Progress, new-segment, and abort callbacks bridge the
C API. Cancellation first sets an atomic flag; only after a grace period may the GUI terminate the helper
process and start a clean one.

The GUI/CLI owns persistence. It sends checksum-bearing model/VAD paths and commits returned chunks; the
worker never writes SQLite or treats stdout as its protocol.

## Consequences

- A model can stay resident across jobs while only one context call runs at a time.
- Worker crashes become Interrupted jobs with committed chunks available for resume.
- Protocol versioning, authentication, frame limits, heartbeat, restart limits, and helper deployment are
  mandatory complexity.
- Every packaged backend variant and nested library must be signed/deployed with the application.
- A protocol change that adds mandatory integrity fields requires a version bump.

## Rejected alternatives

In-process inference would allow native/GPU faults to take down unsaved GUI state. `whisper-cli` would
violate direct integration, provide weaker structured progress/cancel behavior, and require parsing a
process stream. A worker-per-chunk design would discard model reuse and make long jobs slower.

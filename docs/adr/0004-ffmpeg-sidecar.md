# ADR 0004: FFmpeg media sidecar

Status: Accepted — 2026-07-17

## Context

Qt Multimedia playback does not provide one stable cross-platform decoder/export API for every required
audio/video container, while whisper expects 16 kHz mono samples. A bundled decoder must handle Unicode
paths, cancellation, progress, long files, and LGPL-compatible redistribution. This decision concerns
media normalization only; ASR must remain a direct C API integration.

## Decision

Use `ffprobe` and `ffmpeg` through `QProcess` with explicit `QStringList` arguments, separate stdout/stderr,
timeouts, parsed progress, and cooperative cancellation. Never invoke a shell. Write normalized 16 kHz
mono signed PCM16 WAV to a temporary path, validate its RIFF structure/format/size/duration, then atomically
replace cache and generate waveform peaks.

Release scripts build checksum-pinned FFmpeg 8.1.2 with GPL, nonfree, network, and autodetected external
dependencies disabled. Packages include exact `-buildconf`, source record, and LGPL texts.

## Consequences

- Audio and video formats share one deterministic preparation pipeline.
- FFmpeg/FFprobe become signed sidecar binaries that packaging and Diagnostics must locate/version.
- A failed conversion cannot destroy an earlier valid cache; temporary output must be cleaned on every
  exit path.
- Release maintainers must verify LGPL configuration and corresponding source information.

## Rejected alternatives

Reimplementing container/codec support would be large and fragile. Depending on an arbitrary system
FFmpeg would make behavior and licensing non-reproducible. Passing a concatenated shell command would
create quoting/injection failures. Using a CLI for whisper is outside this media-only exception.

# Architecture

BreezeDesk has three composition roots:

- `BreezeDesk`, a Qt Quick GUI that owns persistence, media preparation, scheduling, and presentation;
- `breezedesk-asr-worker`, a Qt Core process that owns libwhisper contexts and inference;
- `breezedesk-cli`, a scriptable client that shares repositories, model management, IPC, exporters, and
  headless worker orchestration.

The worker is a direct libwhisper consumer, not a wrapper around `whisper-cli`. Its process boundary keeps
an inference fault from terminating the GUI while allowing the model to remain resident across jobs.
See [ADR 0002](../adr/0002-native-whisper-worker.md) and the
[worker protocol](worker-protocol.md).

## Target boundaries

| Target | Responsibility |
| --- | --- |
| `breezedesk_core` | Typed errors/results, storage paths, time/text helpers, logging, temp cleanup. |
| `breezedesk_ipc` | Framing, CBOR envelopes, local endpoints, worker client/server, single instance. |
| `breezedesk_database` | SQLite lifecycle, migrations, repositories, paged search. |
| `breezedesk_audio` | FFprobe/FFmpeg, normalized audio validation, recording, waveform/cache. |
| `breezedesk_models` | Immutable manifest, resumable download, checksum and custom-model storage. |
| `breezedesk_glossary` | Profiles/terms, import/export, prompt composition and replacement audit. |
| `breezedesk_transcript` | Segment persistence/editing/autosave/export. |
| `breezedesk_jobs` | Queue, state machine, repositories and interrupted-job recovery. |
| `breezedesk_asr` | Presets, chunk planning, VAD, libwhisper RAII adapters and deduplication. |
| `breezedesk_platform` | Capabilities and native macOS/Windows services. |
| `breezedesk_update` | Update coordinator plus Null/Sparkle/WinSparkle implementations. |
| `breezedesk_ui` | ViewModels, list models, design system, waveform item and QML module. |

Static libraries form an acyclic graph. Core never depends on UI; UI is a leaf over domain interfaces.
QML receives ViewModels and `QAbstractListModel` instances only: it cannot execute SQL, read `QSettings`,
access platform APIs, or see `whisper_context`. Business logic remains in C++.

## Runtime data flow

1. The GUI imports or records media and stores the recording row.
2. FFprobe inspects it; FFmpeg writes validated 16 kHz mono PCM16 and waveform peaks to cache.
3. The scheduler creates the job and durable chunk rows, then starts/selects a worker variant.
4. An authenticated local handshake loads a checksum-verified model. The worker main thread continues
   socket/heartbeat processing while a dedicated QThread runs VAD or `whisper_full`.
5. Progress and partial segments return over CBOR. The GUI transactionally commits each completed chunk
   before requesting the next.
6. Finalization activates the new transcript revision without overwriting older edited jobs.

The database is not shared with the worker. The GUI/CLI persistence owner is therefore the recovery
authority: a worker cannot mark a chunk complete unless its result was accepted and committed.

## Ownership and concurrency

`src/app/main.cpp`, `src/worker/main.cpp`, and `src/cli/main.cpp` construct dependencies explicitly; there
is no global service locator. Native owning pointers use RAII, whisper/VAD contexts use `unique_ptr` custom
deleters, and QObject parentage is explicit. One model session serves one inference at a time. Database
connections are named per thread and never moved between threads.

Platform branches appear only in `src/platform`, native update adapters, packaging, and CMake source
selection. Feature code asks `PlatformCapabilities` rather than scattering `Q_OS_*` checks.

Production logging and privacy boundaries are documented in
[logging-and-diagnostics.md](logging-and-diagnostics.md). Build and dependency rules are in
[build-from-source.md](build-from-source.md).

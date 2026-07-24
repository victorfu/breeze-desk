# Testing

Qt Test executables are independent and labeled `unit`, `integration`, `qml`, `slow`, or `long-form`.
They cover framing/partial packets/malformed lengths/protocol versions/heartbeat/reconnect/races; database
migration/rollback/WAL/recovery/search/trash; chunking/dedup/prompt/cancel/parameters; media metadata,
waveform, Unicode paths and cancellation; model manifest/download/checksum/import/in-use deletion;
glossary audit; transcript editing/revisions/exports; settings isolation; and every QML page/theme/locale.

Ordinary CI uses generated WAVs, a local HTTP fixture server, and fake engine/client test doubles, never
the Breeze model. `BREEZEDESK_ENABLE_MODEL_INTEGRATION_TESTS=ON` enables a tiny official model worker test.
Nightly exercises the deterministic four-hour continuous-speech planning pipeline without running four
hours of inference. It separately runs the checksum-pinned tiny-model worker integration. Full Breeze Q5
and 68-minute media remain manual or explicitly provisioned self-hosted smoke tests; hosted CI does not
claim their performance or quality.

The optional worker test uses `ggml-tiny.en-q5_1.bin` from immutable Hugging Face revision
`c521a4b02f422512d734391fdf08bb08c0862f68`. The download script validates SHA-256 before making the
model available to the test. Run it with:

```sh
./scripts/download-test-model.sh ./build/model-integration/test-models
cmake -S . -B build/model-integration -G Ninja \
  -DBREEZEDESK_ENABLE_MODEL_INTEGRATION_TESTS=ON
cmake --build build/model-integration --parallel
ctest --test-dir build/model-integration -R '^Asr.ModelIntegration$' --output-on-failure
```

The test directly launches `breezedesk-asr-worker`, verifies model loading, progress and partial-segment
callbacks, then sends `CancelJob` during a second inference and requires `JobCancelled`. It never invokes
`whisper-cli`.

## Local test workflow

Configure and run the complete default suite:

```sh
./scripts/build.sh
./scripts/run-tests.sh
```

Or use one build tree directly:

```sh
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure --no-tests=error
```

Useful focused commands include:

```sh
ctest --test-dir build/debug -L Qml --output-on-failure
ctest --test-dir build/debug -R '^(Asr.Core|IPC.FrameCodec|Database.Repository)$' --output-on-failure
```

Tests use `QTemporaryDir` and isolated settings/database identities. IPC tests create user-scoped local
sockets and may require an execution environment that permits them. A failure caused by sandbox denial
must be rerun with the same binary outside that sandbox; it is not acceptable to disable the assertion.

## Coverage organization

Keep files at `tests/<Subsystem>/tst_<Component>.cpp`. Add failure-path coverage with a production change:

- ASR: preset mapping, prompt budget, confidence, timestamp conversion, VAD/chunking, dedup and abort;
- Audio: probe parsing, PCM/WAV validation, Unicode paths, waveform, cancellation, missing tools;
- Database/Jobs: clean and upgrade migrations, rollback, WAL/connections, recovery, revisions and Trash;
- IPC/App: partial/malformed frames, protocol/authentication, heartbeat, reconnect, worker crash and
  single-instance races;
- Models/Glossary/Transcript: resume/checksum/import, CRUD/serialization/audit, edit/undo/export;
- Settings/QML/Utils: defaults/migration/isolation, every page/theme/locale/binding, logging sanitization.

Ordinary tests must not call the public internet or rely on developer-installed media/models. Use the
local HTTP fixture server, generated WAVs, fake engine/client implementations, and helper executables.

## CI tiers

`ci.yml` checks clang-format, dependency-free CMake configure, macOS/Windows native builds and tests,
the CPU fallback worker, and QML smoke. `nightly.yml` validates dependency metadata, runs the four-hour
synthetic planner, and enables the tiny model on macOS. A self-hosted Apple Silicon job may run the full
Breeze Q5 smoke. Reports must distinguish compiled backends from hardware-tested inference.

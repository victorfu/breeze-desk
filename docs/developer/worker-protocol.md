# Worker protocol

Protocol version 2 uses a four-byte unsigned big-endian payload length followed by one CBOR map. Frames
larger than 16 MiB, malformed CBOR, missing required keys, unsupported types, and version mismatches close
the connection with a typed error. Each envelope contains `protocolVersion`, `workerVersion`, `type`,
`requestId`, `jobId`, `sessionToken`, and `payload`.

The endpoint contains the current UID/SID and build identity, uses `QLocalServer::UserAccessOption`, and
requires a random session token passed directly to the spawned worker. Heartbeats run every two seconds;
ten seconds without Pong is a timeout. Messages include Hello/Ack, capabilities, model load/unload,
speech analysis, transcription, progress, partial segments, chunk/terminal completion, cancellation,
errors, Ping/Pong, and Shutdown.

`LoadModel` requires `modelPath` and `modelSha256`. `AnalyzeSpeech` requires `vadModelPath` and
`vadModelSha256`; every `StartTranscription` with VAD enabled requires the same VAD integrity fields.
Checksums are 64 hexadecimal characters. The worker streams the file through SHA-256 on its dedicated
inference thread immediately before calling any whisper.cpp model/VAD load or VAD-enabled
`whisper_full`. A missing, malformed, or mismatched checksum fails closed with a typed error, so corrupt
weights never reach the native model loader. Adding these mandatory fields is why this contract is
protocol version 2.

Cancellation sets an atomic flag observed by whisper's abort callback. After a five-second grace period,
the GUI may kill only the worker process and restart it; it never terminates the inference QThread. Three
unexpected restarts within sixty seconds stop automatic restart and require explicit retry.

## Frame and envelope

The four-byte prefix is an unsigned big-endian payload length. The payload must be exactly one CBOR map
and no larger than 16 MiB. The common envelope keys are:

| Key | Purpose |
| --- | --- |
| `protocolVersion` | Integer contract version; currently 2. |
| `type` | Stable message name from `MessageType`. |
| `requestId` | Correlates commands and replies. |
| `jobId` | Durable job identity for inference messages. |
| `workerVersion` | Build/runtime compatibility and diagnostics. |
| `sessionToken` | Random per-process authentication token. |
| `payload` | Type-specific CBOR map. |

`FrameCodec` accumulates partial socket reads and can emit multiple complete frames. A zero/invalid
length, oversized payload, malformed CBOR, or missing/wrong envelope field returns a typed protocol error
and closes the peer rather than allocating from an untrusted size.

## Session sequence

1. The GUI derives a user/build-scoped endpoint, generates a random token, and starts the worker with that
   token outside stdout IPC.
2. The client sends `Hello`; only a matching protocol and token receives `HelloAck`.
3. `GetCapabilities`/`Capabilities` reports compiled/actual backend, device, whisper version, and worker
   features.
4. `LoadModel`/`ModelLoaded` establishes one checksum-verified model session. `UnloadModel` releases it
   only when idle.
5. Optional `AnalyzeSpeech`/`SpeechAnalysisCompleted` produces streaming VAD regions for chunk planning.
6. `StartTranscription` emits `Progress`, `PartialSegment`, `ChunkCompleted`, and finally
   `TranscriptionCompleted`, `JobCancelled`, or `Error`.
7. `Shutdown` unloads normally. Unexpected socket/process loss is worker failure, not successful
   completion.

`Ping`/`Pong` is independent of inference callbacks: the main worker thread sends/answers heartbeats every
2 seconds and the client declares timeout after 10 seconds. A busy inference QThread must not starve it.

## Integrity and cancellation

`LoadModel` requires `modelPath` and `modelSha256`. `AnalyzeSpeech` requires `vadModelPath` and
`vadModelSha256`; VAD-enabled `StartTranscription` repeats those fields. Digests are 64 hexadecimal
characters and are verified immediately before each native load/use. Integrity failures use typed ASR
errors and never reach libwhisper.

`CancelJob` identifies the active job and sets its atomic abort flag. Cooperative completion returns
`JobCancelled` and retains already delivered data. If native code does not return during the five-second
grace period, the GUI may terminate the helper process, mark the job Interrupted, preserve committed
chunks, and restart within its bounded policy.

## GUI command channel

The single-instance GUI endpoint is distinct from the ASR worker endpoint but reuses the same bounded
frame codec. It is derived from the current user's runtime directory and the build-specific bundle ID,
and the server enables `QLocalServer::UserAccessOption`. `ApplicationCommand` carries a bounded string
array; `ApplicationCommandResult` returns `handled`, `retryable`, an exit code, and separate byte strings
for stdout and stderr. The GUI returns `retryable` without executing while its composition root is still
starting. It returns `handled=false` for commands that should retain their synchronous local CLI
semantics.

The client falls back locally only after a confirmed decline or when it never connected. A disconnect or
timeout after writing the request is classified as indeterminate and fails closed, because transparently
repeating a state-changing command could duplicate work.

Application command payload arrays and reply byte strings are bounded by the same codec. Worker and
single-instance endpoints are distinct, include the current UID/SID and build identity, enable
`QLocalServer::UserAccessOption`, test startup races, and remove only a verified stale endpoint.

Framing, partial packets, malformed lengths/CBOR, protocol/authentication mismatch, heartbeat, cancel,
crash/reconnect, and command indeterminacy are exercised as described in [testing.md](testing.md).

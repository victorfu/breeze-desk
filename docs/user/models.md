# Models

Models are downloaded separately and are never embedded in the repository or installer. The Models page
uses the versioned manifest shipped with BreezeDesk and shows source, immutable revision, license,
quantization, expected size, installation state, and whether a model is loaded.

| Model | Expected size | Use |
| --- | ---: | --- |
| Breeze-ASR-25 Q5_K | 1,080,732,108 bytes | Recommended default, including an 8 GB MacBook Air. |
| Breeze-ASR-25 Q8_0 | 1,656,129,708 bytes | Higher-memory quality option. |
| Silero VAD 6.2.0 | 885,098 bytes | Speech analysis and silence-aligned long-form chunking. |

## Download and verify

Choose **Download** on a model card. The operation follows HTTPS redirects, writes only to a `.part`
file, displays bytes/s and estimated time remaining, and supports pause, resume, cancel, and retry. A
resumed server response is accepted only when its range matches the local partial file; otherwise the
download restarts safely.

After transfer, BreezeDesk streams SHA-256 on a background thread and checks the exact byte size. Only a
match is atomically renamed into the models directory. A corrupt part is removed and cannot be sent to
the worker. The worker performs a second just-in-time SHA-256 check before libwhisper reads ASR or VAD
weights.

Use **Verify** after copying storage, restoring a backup, or diagnosing a load failure. **Test Model**
starts the native worker, loads the selected file, and reports the actual backend and whisper.cpp version;
it is a load/integration check, not a recognition-quality benchmark.

## Default, deletion, and custom files

Set one installed ASR model as the default under Models or **Settings > Transcription**. A file currently
held by the worker cannot be deleted. Finish/cancel active work and unload it before trying again; the UI
explains the in-use state rather than removing a mapped file.

**Import Custom Model** accepts a local whisper.cpp GGML `.bin`, writes a managed copy, hashes it, and
commits a neighboring `.sha256` integrity sidecar. Later discovery and worker loading require that digest
to match. Custom-model quality, vocabulary, memory use, and backend compatibility remain the user's
responsibility.

Every built-in card opens the exact source revision and license. Breeze conversions are Apache-2.0 and
the Silero VAD artifact is MIT according to the bundled manifest. See [Privacy](privacy.md) for the model
download network boundary and [Troubleshooting](troubleshooting.md) for checksum, memory, and GPU errors.

# Transcription

A job requires a verified ASR model and a readable media source. Open a recording and choose
**Transcribe**, or enqueue several recordings and manage their order in Queue. One ASR job runs at a
time; media preparation may proceed separately without freezing the interface.

## Recognition settings

| Setting | Behavior |
| --- | --- |
| Fast | Greedy decoding with minimal fallback for a quick draft. |
| Balanced | Default quality/speed compromise for routine meetings. |
| Accurate | Beam search with beam size 5 and a more conservative quality configuration. |
| `zh` | Explicit Chinese recognition; mixed English terms remain transcription, not translation. |
| `auto` | Lets whisper.cpp detect the language; translation remains disabled. |
| Silero VAD | Finds speech regions and silence-aligned boundaries for long recordings. |

Backend **Auto** selects an available worker variant and can fall back to CPU. Manual CPU, Metal,
and Vulkan choices must exist in the installed package. Initial-prompt behavior combines the
selected glossary, project/meeting context, and a bounded previous-chunk tail. See
[Glossary](glossary.md) before adding a large terminology list.

## Stages and progress

Progress moves monotonically through Preparing, Inspecting media, Normalizing audio, Analyzing speech,
Loading model, Transcribing, Finalizing, and Completed. Long normalized PCM is read in bounded blocks.
Silence-aligned units target roughly ten minutes, have a fifteen-minute maximum, and use overlap only
when a hard speech cut is unavoidable.

Each unit is a durable `job_chunks` record. Its segments are committed to SQLite before the next unit
starts, so completed work survives a GUI, worker, or system interruption. The transcript view displays
partial units, but locks editing while its active revision is still receiving worker results.

## Cancel, retry, and resume

- Cancelling a queued job changes it without starting the worker.
- Cancelling a running job requests cooperative abort; completed chunks and partial diagnostics remain.
- Retry starts the failed work again with recorded diagnostics available in Queue.
- Resume restores the job's original model checksum, preset, language, backend, VAD, prompt snapshot,
  and decoding settings, then selects the first incomplete chunk.

The GUI treats an unexpected worker exit as **Interrupted**, reports the reason, and starts a fresh
worker within its bounded restart policy. It never shares one whisper context across concurrent
inferences.

Settings Diagnostics reports selected/actual backend and the detected whisper.cpp/worker runtime after a
refresh or model load. Queue and the recording revision retain model, backend, progress, and failure
diagnostics for the job. For checksum, GPU, memory, or media failures, follow
[Troubleshooting](troubleshooting.md).

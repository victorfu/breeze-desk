# Long-form transcription

Media is first normalized to 16 kHz mono PCM16 WAV. The planner reads bounded PCM blocks, performs
streaming VAD, targets ten-minute units, prefers eight-to-twelve-minute silence boundaries, and enforces
a fifteen-minute hard maximum. A hard cut adds a 900 ms overlap. Only the current unit becomes float32.

The GUI inserts every planned `job_chunks` row, sends one unit to the worker, transactionally stores its
segments and result hash, then marks it complete. Relative timestamps become global milliseconds. Resume
selects the first incomplete chunk and does not repeat completed inference.

Overlap dedup first limits candidates to the overlap time range. Chinese text uses normalized character
suffix/prefix matching; English uses case-folded tokens. Only a unique high-confidence match is removed.
Ambiguous text is retained with diagnostics. Prompt priority is glossary, meeting context, then the
confirmed previous tail, always within half the model text context.

## Preparation and planning

FFprobe metadata establishes duration and expected output size. FFmpeg writes a temporary PCM16 WAV;
`NormalizedAudioValidator` checks RIFF chunks, format (`16,000 Hz`, mono, signed 16-bit PCM), data length,
and duration tolerance before atomic replacement. Waveform generation reads the validated output and
writes reusable downsampled peaks.

Audio shorter than 12 minutes can remain one transcription unit. Longer audio is read from disk in
bounded PCM16 blocks. The VAD adapter converts only the current aligned block to float samples and copies
probabilities before the next whisper VAD call overwrites its buffer. `StreamingVadSegmenter` uses a 0.5
threshold, 250 ms minimum speech, 100 ms minimum silence, and 30 ms padding by default.

`LongFormChunkPlanner` uses these millisecond defaults:

| Parameter | Value |
| --- | ---: |
| Short-audio threshold | 720,000 |
| Preferred minimum / target / maximum | 480,000 / 600,000 / 720,000 |
| Hard maximum | 900,000 |
| Hard-cut overlap | 900 |

The planner selects a silence boundary in the preferred window. It cuts within speech only after the
hard maximum and records overlap on both adjacent chunk rows.

## Durable execution

Planning inserts every `job_chunks` row before inference. For each ordinal, the coordinator:

1. reads only that PCM range and converts it to float32;
2. composes a token-bounded prompt from the durable job snapshot;
3. calls the worker and translates chunk-relative centisecond timestamps to global milliseconds;
4. checkpoints partial segments as provisional data;
5. transactionally replaces/finalizes the chunk's segments, result hash, diagnostics, and completion;
6. advances `last_completed_chunk` and requests the next ordinal.

A cancellation or worker failure never marks an uncommitted chunk complete. Resume retains completed
rows, increments attempts for retried work, and restores the original job parameters rather than reading
new defaults.

## Overlap and prompt rules

Deduplication runs only when timestamps indicate overlap. Text is normalized into case-folded Latin word
tokens and individual CJK code points, then compares the previous suffix with the incoming prefix. At
least two matching units are required before deletion. A one-unit or otherwise ambiguous match is kept
and recorded in diagnostics.

Prompt priority is high-priority glossary terms, project/meeting context, then the confirmed previous
tail. `whisper_tokenize` measures the actual loaded vocabulary in two passes. The composer never uses
more than `whisper_n_text_ctx()/2`; omitted terms are reported to the preview and durable diagnostics.

The deterministic planner/deduplicator and a four-hour synthetic pipeline are covered by
[testing.md](testing.md). Worker-side details are in [whisper-integration.md](whisper-integration.md).

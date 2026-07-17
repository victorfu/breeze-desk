# ADR 0006: Resumable long-form chunks

Status: Accepted — 2026-07-17

## Context

Meetings range from 30 minutes to several hours. Keeping the whole float32 stream in memory is unsuitable
for an 8 GB laptop, while one monolithic `whisper_full` call provides a poor recovery boundary. Blind fixed
cuts can split speech and duplicate or lose text.

## Decision

Normalize once to disk-backed 16 kHz mono PCM16. Analyze VAD in bounded blocks, keep files shorter than 12
minutes as one unit, otherwise target ten-minute silence-aligned units in an 8–12 minute preferred window.
Enforce a 15-minute hard maximum and add 900 ms overlap only when a speech cut is unavoidable.

Persist the complete chunk plan before inference. Convert only the current unit to float32, call
`whisper_full`, translate timestamps to global milliseconds, and commit its segments/result hash before
starting the next ordinal. Resume selects the first incomplete chunk with the original job parameter and
prompt snapshot.

Deduplicate only within a known timestamp overlap. Compare case-folded English tokens and CJK characters;
require at least two deterministic matching units. Retain ambiguous text and diagnostics.

## Consequences

- Peak audio memory is bounded by the current chunk rather than total duration.
- Worker/GUI failure loses at most uncommitted current-unit work and never repeats completed chunks.
- Long recordings require normalization cache, VAD planning, chunk tables, prompt budgeting, and careful
  global timestamp conversion.
- Hard cuts may retain ambiguous duplicate text by design; avoiding data loss takes precedence over a
  cosmetically perfect merge.

## Rejected alternatives

Loading all float32 PCM scales poorly. One transcription call has no durable mid-file resume. Fixed cuts
ignore speech boundaries. Aggressive fuzzy deduplication can silently delete legitimate repeated phrases,
which is unacceptable for a transcript editor.

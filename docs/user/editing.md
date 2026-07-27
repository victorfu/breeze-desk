# Editing transcripts

The recording view keeps the player, waveform, and virtualized segment list synchronized. Click a segment
timecode to seek; during playback the current segment is highlighted and, when auto-scroll is enabled,
kept in view. The player supports 0.5× through 2× speed, ±5-second seeking, volume/mute, and looping a
waveform selection.

## Edit and review segments

- Edit the displayed text inline. `original_text` remains unchanged; corrections are saved in
  `edited_text`.
- **Split** divides the selected segment at the current playhead. The playhead must be strictly inside
  the segment.
- **Merge previous/next** combines adjacent text and uses the combined time range.
- Delete removes the selected segment from the transcript. Undo and redo operate on editor
  snapshots until a new load replaces the stack.
- Mark a segment reviewed, or use the low-confidence filter to focus on uncertain recognition.
- A glossary badge exposes each audited alias replacement. Apply or undo it individually without
  rewriting the original ASR text.

Split and merge operations must keep times non-negative, have `end > start`, and preserve ordering without
an invalid overlap. A rejected operation leaves the previous segment state intact. **Save Changes**
persists the current snapshot; autosave also commits after editing settles. Ctrl/Cmd+S forces an
immediate save.

## Find and navigate

The transcript search field filters matching segment text. **Previous** and **Next** move between
matches; Ctrl/Cmd+F focuses transcript search. Space controls playback only when a text editor does not
hold focus, so typing cannot accidentally start audio.

## Repeated transcription

Each recording has one current transcript. Starting recognition again keeps the current transcript in
place and locks editing while the new job runs. Only a successful completion replaces it and removes the
previous transcript segments. A failed, cancelled, or interrupted attempt leaves the current transcript
unchanged. For a recording without a completed transcript, partial segments may be shown while processing
is active.

See [Exporting](exporting.md) for format guarantees and [Glossary](glossary.md) for replacement audits.

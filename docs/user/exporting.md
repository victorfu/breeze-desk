# Exporting

Open the active recording revision, save pending edits, then choose **Export** or press Ctrl/Cmd+E. The
GUI dialog selects destination and format. The CLI additionally accepts `--timecodes` for TXT and `--bom`
for tools that need a UTF-8 BOM. The export uses displayed edited text when present while formats that
support audit data retain the ASR original.

| Format | Output contract |
| --- | --- |
| TXT | One edited segment per line; optional `[timecode]` prefix. |
| Markdown | YAML-style title/recording/model/language metadata followed by timestamped paragraphs. |
| SRT | Sequential numeric cues with comma-millisecond timestamps. |
| VTT | `WEBVTT` header and period-millisecond cue timestamps. |
| JSON | Stable `schemaVersion: 1` object with recording, engine, metadata, and segment arrays. |
| CSV | `start,end,original_text,edited_text,confidence` in milliseconds with RFC-style field quoting. |

JSON segments include ids, millisecond ranges, original/edited text, average/minimum/no-speech
probabilities, low-confidence and reviewed flags, and glossary audit. Its engine object always contains
model id/checksum, engine/worker version, language, and preset keys; values reflect metadata available to
the active GUI/CLI export path.

Before rendering, segments are sorted by time. Negative starts are clamped to zero, every end is at least
one millisecond after its start, and a cue cannot begin before the prior cue ends. Subtitle text uses the
v1 default maximum of 42 characters per line, preferring a nearby space but supporting Chinese text
without spaces.

All formats are UTF-8 with normalized line endings. The optional BOM supports Windows tools that do not
detect UTF-8 reliably. BreezeDesk writes through `QSaveFile`, so a short write, permission failure, or
full disk cannot replace a previously valid destination with a partial export. Confirm overwriting an
existing file and choose another directory when the destination is read-only.

For scriptable exports and stable exit codes, see the [CLI](cli.md).

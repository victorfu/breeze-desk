# Privacy

BreezeDesk processes imported media, microphone recordings, normalized PCM, waveform peaks, models,
jobs, glossary data, and transcripts locally. It has no account, telemetry, analytics SDK, crash uploader,
cloud recognition, summarization, chatbot, collaboration service, or synchronization.

## Network boundary

Only two features may create an outbound request:

- a model download, pause/resume, or retry that the user starts from Models or the CLI;
- an application update check when native updates are available and the user enables or invokes it.

Built-in model URLs are immutable manifest revisions. Update checks are disabled in Debug and do not
block application startup. Import, playback, recording, normalization, ASR, editing, search, glossary
processing, and export do not require a network connection.

## Local data and deletion

Settings shows the application-data, model, database, cache, export, and log locations. Reference imports
remain at their original path; managed imports create a local application-owned copy. Trash is a soft
delete. Permanent delete removes the selected recording's database rows, managed copy, normalized cache,
and waveform cache, but never an original source outside managed storage.

Successful, cancelled, and failed operations clean their temporary files. Startup also removes stale
temporary artifacts. **Clear Cache** can remove reproducible media cache without removing the database,
models, source recordings, or exported documents.

## Logs and diagnostics

GUI and worker logs are category-based, size-rotated, and retained for a bounded period. They do not log
audio samples or complete transcript/glossary content by default. Path redaction replaces personal path
segments before disk output when enabled.

**Export Sanitized Diagnostics** writes system/app versions, protocol and worker runtime state,
selected/actual backend, FFmpeg/whisper versions when detected, sanitized settings, and rotated logs. It
excludes audio, transcripts, glossary entries, and personal paths unless the user explicitly chooses
otherwise. Inspect the archive before sharing it and follow [Troubleshooting](troubleshooting.md) to
reproduce an error without private media.

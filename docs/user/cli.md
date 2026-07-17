# Command-line interface

`breezedesk-cli` uses the same database, model manager, protocol, worker, and exporters as the GUI.

```text
breezedesk-cli transcribe <file> --model breeze-asr-25-q5 --language zh \
  --preset balanced --glossary <profile-id-or-name> --vad \
  --output meeting.srt --format srt
breezedesk-cli transcribe <file> --resume-job <interrupted-or-failed-job-id> \
  --output meeting.srt --format srt
breezedesk-cli import <file...>
breezedesk-cli library list
breezedesk-cli library search <query>
breezedesk-cli jobs list
breezedesk-cli jobs cancel <id>
breezedesk-cli models list
breezedesk-cli models download <id>
breezedesk-cli models verify <id>
breezedesk-cli export <recording-id> --format json
```

By default the CLI first probes the running GUI's user-scoped local endpoint. The GUI accepts commands
whose state it owns:

- `import <file...>` schedules the imports in the visible library.
- Bare `transcribe <file>` imports the file when necessary and queues a job. It reports `Queued` or
  `Importing`; it does not wait for an exported transcript.
- `jobs cancel <id>` cancels through the GUI scheduler, including its active worker.
- `models download <id>` starts or reuses the download shown on the Models page.

Read-only commands keep their existing local stdout schema. Transcription commands with decoding,
glossary, resume, format, or output options also run locally so that they preserve the synchronous export
contract shown above. The same is true for `export`. Add the global `--headless` flag anywhere in a
command to bypass GUI probing explicitly. A native ASR worker is started only when that local command
needs inference; read-only headless commands do not start one.

Forwarded `--json` responses include `"forwarded": true` and a stable state field. If no GUI is running,
or if it explicitly declines a command, the CLI safely executes it locally. If the request was delivered
but its reply cannot be confirmed, the CLI exits with code 12 instead of risking a duplicate import,
download, cancellation, or job. Machine-readable output goes to stdout; progress and diagnostics go to
stderr. All exit codes are documented by `breezedesk-cli --help`.

Local transcription creates the recording, job, and chunk plan before media normalization begins.
Every partial segment is checkpointed in SQLite. If the process is interrupted, the native worker
exits, or a chunk fails, the CLI prints the durable job ID to stderr; pass it to `--resume-job` with the
same source file. Completed chunks are retained and only interrupted or failed chunks run again. Resume
also restores the original
model, language, preset, backend, VAD, prompt, and flash-attention settings rather than silently mixing
parameters within one transcript revision.

`--glossary` snapshots the selected profile and enabled terms into the durable job. The native worker
prioritizes glossary entries, meeting context, and previous-chunk context with the model's real tokenizer;
explicit aliases are audited while the ASR text remains preserved as `originalText`.

## Exit codes

| Code | Meaning |
| ---: | --- |
| 0 | Success |
| 2 | Invalid command or option |
| 3 | Source file missing |
| 4 | Model unavailable or invalid |
| 5 | Media inspection/normalization failure |
| 6 | Worker startup, protocol, or transport failure |
| 7 | Transcription failure |
| 8 | Database failure |
| 9 | Export failure |
| 10 | Model-download/network failure |
| 11 | Cancelled |
| 12 | Internal or indeterminate forwarded-command failure |

Shell scripts should treat stdout as the result channel and stderr as human diagnostics/progress. Use
`--json` for stable machine-readable output and still check the process exit status; a JSON error does
not turn a failed command into success. Use `--headless` when deterministic ownership matters, such as a
scheduled job where a running GUI must not accept an import or cancellation.

See [Exporting](exporting.md) for each file contract, [Models](models.md) for integrity behavior, and
[Transcription](transcription.md) for durable resume semantics.

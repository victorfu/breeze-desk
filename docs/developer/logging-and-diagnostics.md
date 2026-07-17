# Logging and diagnostics

BreezeDesk uses Qt logging categories and a process-local `ApplicationLogger`. GUI, ASR worker, and CLI
install separate logger instances because they are separate processes. A normal installation uses the
central `StoragePaths::logs()` directory and process names such as `BreezeDesk`,
`breezedesk-asr-worker`, and `breezedesk-cli`.

Each entry contains an ISO 8601 UTC timestamp, the process name and PID, severity, category, and the
message. Files rotate before the configured size limit and only the configured number of generations is
retained. Log files are created with owner-only permissions where the operating system supports them.

The Qt message handler sanitizes entries before either disk or standard-error output. It redacts local file
paths, session and authentication values, transcript text, original or edited text, glossary content,
meeting context, prompts, and audio or PCM content. New code must log identifiers, state transitions,
counts, timing, error codes, and backend metadata—not audio, transcript, glossary, prompt, token, or
personal path values. Wrap any additional sensitive diagnostic fragment in
`[private]...[/private]`; the marker and its content are removed before output. Do not bypass the Qt
category APIs by writing production diagnostics directly to stdout, stderr, or another file.

Typical composition-root setup is:

```cpp
BreezeDesk::LoggingConfiguration logging;
logging.processName = QStringLiteral("BreezeDesk");
logging.logDirectory = BreezeDesk::StoragePaths::logs();
BreezeDesk::ApplicationLogger logger(logging);
const auto result = logger.install();
```

Keep the logger alive until the event loop exits. `uninstall()` restores the message handler that was
present before BreezeDesk installed its logger. The implementation never forwards unsanitized content to
that previous handler while active.

At startup, a process may call `TemporaryFileJanitor::clean()`. The default policy only removes files
older than 24 hours below `StoragePaths::temporary()`. In-use paths can be supplied through
`protectedPaths`. Cleanup rejects filesystem roots, the user home directory, the application data root,
and locations outside the application temporary directory by default. It does not follow directory
symlinks.

Logging and cleanup perform no network requests, telemetry, analytics, or crash upload. Diagnostics export
applies an additional sanitizer and excludes audio, transcript, glossary, and personal paths unless the
user explicitly opts into path inclusion.

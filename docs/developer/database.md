# Database

SQLite runs with foreign keys, WAL, a 5-second busy timeout, and one named connection per thread. Startup
applies numbered migrations in transactions, backs up an existing database before schema changes, runs
`quick_check`, and converts active jobs from a previous unclean shutdown to Interrupted. A migration
records its version only after the entire transaction commits.

Recordings are soft-deleted. Jobs are immutable transcription revisions; the recording points to its
active revision. Chunks record range, overlap, attempts, state, error, and result hash. Segments retain
both original and edited text plus confidence and replacement audit. Permanent deletion removes only
BreezeDesk-managed media/cache and never the user's source.

FTS5 indexes title, notes, tags, original text, and edited text when available. The fallback executes
escaped `LIKE` queries over the same fields with paging, so search remains functional in Qt builds that
lack FTS5.

## Location and connections

The Release database is `database/breezedesk.sqlite3` below `QStandardPaths::AppLocalDataLocation`; Debug
uses its separately branded data root. Only repositories issue SQL. QML receives models/ViewModels and
the worker never opens the database.

`DatabaseManager` creates a connection name from its instance id and current QThread address. Callers
must request a connection on the thread that uses it and release queries before shutdown; a
`QSqlDatabase` connection is never shared across threads. Every connection enables `foreign_keys`, a
5,000 ms `busy_timeout`, and WAL when configured.

## Schema and migrations

Schema version 6 contains these durable groups:

- `recordings`, tags, and `recording_tags` for Library/Trash metadata;
- `transcription_jobs`, `job_chunks`, and `transcript_segments` for revision and resume state;
- `glossary_profiles` and `glossary_terms`;
- `installed_models` and `database_features`;
- `schema_migrations`, FTS5 `search_index` when available, and `search_index_fallback` always.

Migration history is checksummed and verified at every startup:

| Version | Name | Change |
| ---: | --- | --- |
| 1 | `initial_schema` | Core recording/job/chunk/segment/tag/glossary/model tables. |
| 2 | `query_indexes` | Queue, recording, segment and glossary indexes. |
| 3 | `search_index` | FTS5 probe plus always-available fallback index and feature record. |
| 4 | `queue_visibility` | `transcription_jobs.queue_hidden`. |
| 5 | `recording_source_index` | Index for exact source-path lookup. |
| 6 | `segment_review_state` | Per-segment `reviewed` flag. |

Before upgrading a non-empty older schema, `VACUUM INTO` creates a consistent timestamped backup.
Statements and migration-row insertion share one transaction; failure rolls back. A database newer than
the application, a missing version, or a name/checksum mismatch fails closed instead of guessing.

## Job recovery and revisions

The job row stores model/checksum, engine/worker versions, backend, language, preset, glossary/context,
parameters, diagnostics, progress, and last completed chunk. Each chunk has ordinal/range/overlap,
attempts, state, error, diagnostics, and result hash. Each segment links recording, job, and optional
chunk while preserving original/edited text, confidence, provisional state, replacement audit, and
review state.

Startup recovery changes nonterminal work left by an abnormal exit to **Interrupted**. Resume queries
the first incomplete chunk and does not delete completed segment rows. A new transcription creates a new
job/revision and changes `recordings.active_job_id` only when selected; it never overwrites an earlier
edited revision.

## Search, deletion, and backup

Search documents combine recording title, notes, tags, original transcript, and edited transcript. FTS5
uses Unicode tokenization; the fallback uses escaped, paged `LIKE` predicates over equivalent data.
Repository operations refresh the index after relevant writes.

Trash sets `deleted_at`; restore clears it. Permanent delete relies on foreign-key cascades for database
content, while the application separately validates that any media/cache path is managed before deleting
it. Original source paths are never deletion targets. Manual backups use the same consistent
`VACUUM INTO` snapshot and atomic `QSaveFile` copy.

See [long-form-transcription.md](long-form-transcription.md) for chunk commit ordering and
[testing.md](testing.md) for migration/recovery coverage.

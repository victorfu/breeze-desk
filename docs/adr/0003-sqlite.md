# ADR 0003: SQLite persistence

Status: Accepted — 2026-07-17

## Context

Recordings, revisions, thousands of segments, queue state, glossary terms, model metadata, Trash, and
crash-resumable chunks need transactional local persistence. The app cannot require a server or account,
and searches must work even when the Qt SQLite driver lacks FTS5.

## Decision

Use Qt SQL with SQLite, foreign keys, WAL, a five-second busy timeout, and one connection per thread.
Numbered migrations are checksummed, backed up before upgrade, and committed transactionally. Startup
runs `PRAGMA quick_check` and marks abandoned nonterminal jobs Interrupted. Recordings use soft delete;
jobs are immutable revisions and chunks are the resume checkpoint.

Probe FTS5 during migration and record the capability. Maintain an equivalent fallback search table and
paged escaped `LIKE` query path so search never disappears.

## Consequences

- The database remains a single portable local file with atomic chunk/editor writes.
- Repositories, not QML, own SQL and transaction boundaries.
- Connections cannot cross threads; async operations must acquire their own named connection.
- Migration history mismatch or a newer schema fails closed and needs an explicit application upgrade.
- Search behavior must be tested in both FTS5 and fallback modes.

## Rejected alternatives

Flat JSON cannot provide safe concurrent transactions, indexed search, relations, or partial recovery at
this scale. A network database conflicts with offline deployment. Requiring FTS5 would break otherwise
valid Qt distributions; silently disabling search is not acceptable.

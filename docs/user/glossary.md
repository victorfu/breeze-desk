# Glossary

A glossary profile groups terminology for one project, customer, or recurring meeting. Give it a clear
name, an optional description, and short project context. Duplicate a profile before adapting a shared
list; deleting a profile also removes its owned terms after confirmation.

Each term has canonical text, zero or more aliases, category, language, integer priority, case-sensitivity,
enabled state, and notes. Search examines canonical text, aliases, and notes. Disable a term to retain it
without using it in prompts or alias correction; priority determines which entries survive a tight token
budget.

## Prompt construction

Select a profile on the Glossary page and maintain its project context. The selection becomes the default
profile snapshotted by the next GUI job. Before each chunk, BreezeDesk composes natural-language prompt
sentences in this order:

1. enabled high-priority glossary terms;
2. the selected profile's project context, stored with the job;
3. a short confirmed tail from the previous chunk.

The loaded whisper tokenizer measures the actual token count. Content stops at half of
`whisper_n_text_ctx()`, and the preview shows the final prompt, limit, and omitted terms. The whole
glossary is never appended blindly. Disabling initial prompts in Transcription Settings bypasses this
composition without deleting the profile.

## Import and export

JSON uses `schemaVersion: 1`, a `profile` object (`id`, `name`, `description`, `projectContext`), and a
`terms` array. CSV imports terms into the selected profile and requires this exact header:

```csv
canonical_text,aliases,category,language,priority,case_sensitive,enabled,notes
```

Separate multiple aliases with `|`; quote CSV fields containing commas or quotes. Boolean values are
`true`/`false`, and `priority` must be an integer. Files are UTF-8 and an optional BOM is accepted.
Malformed headers, rows, or unsupported JSON schema versions are rejected without partially replacing a
valid profile.

## Conservative correction

After recognition, only explicit alias mappings are applied automatically by default. Replacements are
processed by priority, do not rewrite sentences, and are stored as per-segment audit objects containing
the term, original text, canonical text, location, and applied state. Review the glossary indicator in
the [editor](editing.md) to accept or undo each item. Fuzzy replacement remains disabled.

# Name Dictionary

BreezeDesk keeps one shared name dictionary. Add the spelling you want in transcripts and, optionally,
other ways the name may be recognized. Use the switch on each name to include or exclude it from future
transcription jobs. Existing installations consolidate
terms from older glossary profiles into this shared list during database migration. Unique terms keep
their IDs and enabled states; same-name entries are consolidated deterministically.

Each term has canonical text, zero or more aliases, category, language, integer priority, case-sensitivity,
enabled state, and notes. Search examines canonical text, aliases, and notes. Disable a term to retain it
without using it in prompts or alias correction; priority determines which entries survive a tight token
budget.

## Prompt construction

The next GUI job snapshots the enabled terms from the shared glossary. Before each chunk, BreezeDesk
composes natural-language prompt sentences in this order:

1. enabled high-priority glossary terms;
2. a short confirmed tail from the previous chunk.

The loaded whisper tokenizer measures the actual token count. Content stops at half of
`whisper_n_text_ctx()`. The whole glossary is never appended blindly. Disabling initial prompts in
Transcription Settings bypasses this composition without deleting or disabling individual terms.

## Conservative correction

After recognition, only explicit alias mappings are applied automatically by default. Replacements are
processed by priority, do not rewrite sentences, and are stored as per-segment audit objects containing
the term, original text, canonical text, location, and applied state. Review the glossary indicator in
the [editor](editing.md) to accept or undo each item. Fuzzy replacement remains disabled.

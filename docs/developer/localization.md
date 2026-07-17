# Localization

All user-facing C++ strings use `tr()` or `QCoreApplication::translate`; QML uses `qsTr()`. Sources are
updated and released with `scripts/update-translations.*` and `scripts/release-translations.*` into
`breezedesk_en.ts` and `breezedesk_zh_TW.ts`. The language manager replaces translators and calls
`QQmlEngine::retranslate()`; transcript content is never translated or locale-converted.

Dates, durations, byte counts, and numbers use the selected locale at the presentation boundary.
Identifiers, JSON schemas, IPC values, database timestamps, and export machine formats remain stable.

## Update the catalogs

After changing a visible string, run:

```sh
./scripts/update-translations.sh
```

On Windows use `scripts\update-translations.bat`. Translate every new message in both
`translations/breezedesk_en.ts` and `translations/breezedesk_zh_TW.ts`; do not translate ids, model names,
command options, or transcript content. Preserve `%1`, `%2`, and `%n` placeholders and supply the locale's
required numerus forms.

Generate catalogs locally with:

```sh
./scripts/release-translations.sh
```

`qt_add_translations` embeds the resulting `.qm` resources below `/i18n`; generated `.qm` files are
ignored rather than committed. A release check should reject `type="unfinished"`, malformed XML, missing
placeholders, and a QML binding warning caused by translation length.

## Runtime switching

The Settings ViewModel persists only `en` or `zh_TW`. The application removes the current `QTranslator`,
loads `:/i18n/breezedesk_<locale>.qm`, installs it, and calls `QQmlEngine::retranslate()`. ViewModels emit
their translated properties again where a stored value is presented as a label. Stable enum/database/IPC
values remain English identifiers and are mapped to translated labels at the UI boundary.

Test both directions without restarting, including dialogs created before and after the switch, theme
changes, plural messages, long error text, and accessibility names. See [testing.md](testing.md).

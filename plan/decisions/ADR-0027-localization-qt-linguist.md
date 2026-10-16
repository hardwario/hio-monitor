# ADR-0027 — Localization: English and Czech with Qt Linguist

- **Status:** Accepted (amended by ADR-0103: everything sent over MCP is English, in the GUI too)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

open-questions #4 asked whether the app is English only or also Czech, and it was scheduled for M3.
The requirement is now that the app is in **English and Czech**, with the possibility of adding more
languages later. The M0 plan (still Draft) already defines `hio::Error` with user-visible English texts
(`message`, `cause`, `suggestion`), so the decision affects the backend, not only the QML.

## Options considered

- **When backend texts are translated:**
  1. At creation: the backend calls `QCoreApplication::translate` (QtCore, allowed by ADR-0004) and
     `hio::Error` holds already-translated UTF-8 text.
  2. At presentation: `hio::Error` carries English source text + context + arguments, and each consumer
     (GUI, CLI, MCP) translates when rendering. Needs a message-template mechanism for every error.
- **CLI language:** English only / follow locale / follow locale but `--json` English.
- **Tooling:** Qt Linguist with English source text / Qt Linguist with text IDs (`qsTrId`) / own catalogs.
- **Language switch in the GUI:** live / after restart.

## Decision

- v2.0 ships **English (`en`) and Czech (`cs`)**. English is the source language.
- **Qt Linguist** with **English source strings** (`tr()`, `qsTr()`, `QCoreApplication::translate`,
  `Q_DECLARE_TR_FUNCTIONS`). One project catalog per language: `i18n/hio_<lang>.ts`
  (`hio_en.ts` holds plural forms only, `hio_cs.ts` the full translation). `.qm` files are compiled
  by `lrelease` and embedded as resources into the GUI executable only.
- **Translate at creation.** Backend libraries translate user-visible texts where they create them;
  `hio::Error` keeps holding UTF-8 `std::string`s in the process language. The stable machine
  contract is the error `code` id (ADR-0024), never the text.
- Translatable texts with values use `%1` placeholders and `QString::arg` (word order differs between
  languages); `std::format` is never used to build translatable text.
- **The CLI is English only**: it never installs a translator, so its output is stable for scripts,
  CI, support and py-hardwario users. This includes `--json` and MCP over stdio.
- **GUI language setting** `ui/language` = `system` (default) | `en` | `cs`. Changing it applies
  **live** (`QCoreApplication::installTranslator` + `QQmlEngine::retranslate()`). Backend texts that were
  created before the switch (e.g. an error panel already on screen) keep their language until they are
  produced again — documented, acceptable.
- Supported languages are listed in one table in `libs/core` (`supportedLanguages`: code + native name).
  **Adding a language = a new `.ts` file + one table entry.**
- Qt's own translations (`qtbase_cs`, `qtdeclarative_cs`) are loaded too and shipped by packaging (M7).

## Consequences

- Every user-visible string in `libs/*` and `app/gui` must be translatable; reviews and clang-tidy
  cannot fully enforce this, so the `.ts` freshness check in CI (lupdate diff) and translation reviews do.
- Numbers and dates in the GUI follow the selected language's `QLocale`; the CLI formats with the C locale.
- Wizard illustrations and photos avoid embedded text (text goes into translatable captions).
- Czech translation becomes part of each feature's definition of done for GUI-visible texts; unfinished
  `cs` entries are reported in CI and must be 0 before a release (M7).
- ~~MCP served by the GUI (M6) returns texts in the GUI language; clients rely on `code` ids.~~ Superseded by
  ADR-0103: MCP texts are English in the GUI and the CLI.

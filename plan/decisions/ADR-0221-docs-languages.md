# ADR-0221 — User pages in all UI languages, reference pages in English

- **Status:** Accepted (supersedes the language part of ADR-0138)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0029 session)

## Context

ADR-0138 chose English-only documentation for v2.0. The UI is translated (ADR-0027) and plan 0030 adds German, Slovak
and Polish. tower-bridge ships its manual in the same five languages. The CLI is English only (ADR-0027).

## Options considered

1. English only (ADR-0138).
2. The user pages in English and Czech.
3. **The user pages in English, Czech, German, Slovak and Polish; the reference pages in English (chosen).**
4. Every page in five languages.

## Decision

- Layout `docs/<lang>/<page>.md` with `<lang>` = `en`, `cs`, `de`, `sk`, `pl`.
- Translated (user pages): `install`, `getting-started`, `console`, `flashing`, `bluetooth`, `troubleshooting`,
  `privacy`, plus an `index.md` per language.
- English only, in `docs/en/`: `cli.md`, `cli-migration.md`, `mcp.md`; the translated pages link to them.
- UI names in a translation are the strings of that language's `.ts` catalog (where plan 0030 has not translated
  them yet, the English UI string). The translations are written by the agent; Czech is reviewed by the user, German,
  Slovak and Polish by native speakers later (open point of plan 0029, like plan 0030).

## Consequences

- Every change of a user page must be made in five files; the English page is the reference.
- Error messages quoted in `troubleshooting.md` follow the app's own translations where they exist.

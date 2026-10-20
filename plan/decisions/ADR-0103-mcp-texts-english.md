# ADR-0103 — MCP texts are English in the GUI and the CLI (amends ADR-0027)

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

ADR-0027 says the GUI's MCP returns texts in the GUI language and clients rely on `code` ids. AI clients work
best with English, support sees the same text as the user's AI, and the CLI is already English only.

## Decision

- Everything sent over MCP is **English**: tool and prompt descriptions, results, `hio::Error` message, cause and
  suggestion. This holds for both the GUI and the CLI server. Errors are rendered with the untranslated English
  source texts, the same way as the CLI does it.
- The GUI side of MCP (settings section, consent dialogs, console markers, activity list) stays translated
  (EN + CS).
- ADR-0027 gets an "Amended by ADR-0103" note. Its MCP sentence no longer applies.

## Consequences

- No locale-dependent strings in `libs/mcp`. Tests can compare golden JSON files.

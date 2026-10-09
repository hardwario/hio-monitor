# Plan — HARDWARIO Monitor v2

This folder is the single source of truth for *what* we build and *why*.
Code follows plans, never the other way round.

## Contents

| Document | Purpose |
|---|---|
| [0000-vision.md](0000-vision.md) | Goals, non-goals, success criteria |
| [0001-architecture.md](0001-architecture.md) | Modules, boundaries, threading, error model |
| [0002-legacy-inventory.md](0002-legacy-inventory.md) | What the old app and py-hardwario do; parity checklist; bugs not to repeat |
| [0003-ux-and-wizards.md](0003-ux-and-wizards.md) | UX principles, wizards, error presentation |
| [0004-mcp.md](0004-mcp.md) | Integrated MCP server design |
| [0005-build-ci-release.md](0005-build-ci-release.md) | Toolchains, CMake, CI, packaging |
| [0006-cli.md](0006-cli.md) | Command-line interface (py-hardwario compatible) |
| [gui/design-system.md](gui/design-system.md) | GUI design tokens, components, accessibility; [mockups](gui/mockups/index.html) |
| [roadmap.md](roadmap.md) | Milestones; each becomes one or more feature plans |
| [open-questions.md](open-questions.md) | Decisions not yet taken |
| [decisions/](decisions/) | Architecture Decision Records (ADRs) |
| [features/](features/) | Feature plans (`NNNN-short-name.md`) |

## Process

1. **Draft** — author creates `features/NNNN-name.md` from [TEMPLATE-feature.md](TEMPLATE-feature.md).
   Number = next free four-digit number.
2. **Review** — every important decision in the plan is asked of the user explicitly.
   Confirmed decisions become ADRs ([TEMPLATE-adr.md](TEMPLATE-adr.md)).
3. **Approved** — the user approves; the plan is frozen except for its checklist and status.
4. **In progress** — a separate implementation agent takes the plan, implements it step by step,
   ticks the checklist. Any deviation → stop, update plan, ask the user.
5. **Done** — verification section passes; status updated; roadmap updated.

Statuses: `Draft`, `Approved`, `In progress`, `Done`, `Rejected`, `Superseded by NNNN`.

## Rules for implementation agents

- Read `CLAUDE.md`, this file, `0001-architecture.md` and the ADRs referenced by the plan first.
- Implement only what the plan says. Out-of-scope findings go to `open-questions.md` or a new Draft plan.
- Keep the plan's checklist in sync with the actual state of the code.

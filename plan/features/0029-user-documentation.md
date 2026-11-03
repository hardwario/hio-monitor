# 0029 — User documentation

- **Status:** Done 2026-11-03 (approved by Matej Strnad 2026-11-03; review of the EN/CS text by the user and of DE/SK/PL
  by native speakers open)
- **Milestone:** M8 (phase E of [0017](0017-m8-parity.md), taken out as its own plan; [0028](0028-gui-feedback-round-2.md) F7)
- **Author:** Matej Strnad (with Claude), 2026-11-03
- **Related ADRs:** ADR-0027, ADR-0138, ADR-0139, ADR-0203, ADR-0206, ADR-0220, ADR-0221, ADR-0222
- **Depends on:** the integrated `v2-dev` build (commit 770a51f); plan 0030 (DE/SK/PL UI strings, for the UI names
  in those pages)

## Context

v2 has no README and no user documentation outside the app. ADR-0138 (2026-10-19) chose `docs/` in the repository
as the source of truth, English only, with a page per topic, `cli.md` generated from `hwio --help` and the in-app
links pointing to GitHub; publishing to docs.hardwario.com was left open (open question 29). Plan 0028 moved the
customer documentation to this plan.

The sibling project `tower-bridge` got documentation the user likes: a README that works as a product page
(features, download table, first-run notes per OS, building, layout, licence) linking into `docs/`, a manual in five
languages, and English reference pages (`cli.md`, `api.md` for REST + MCP, `linux.md`, `release.md`). This plan takes
its README, tone and depth, and keeps ADR-0138's page per topic.

The repository moves to **`hardwario/monitor`** (D8); every absolute repository URL must be easy to replace.

## Goals / Non-goals

**Goals**
1. `README.md` at the repository root as the product page (ADR-0222).
2. User pages in English, Czech, German, Slovak and Polish (ADR-0221): install (per OS: packages, SEGGER J-Link
   Software ≥ 8.00, udev and Bluetooth on Linux, macOS permissions, nRF Util for the modem), getting started (first-run
   setup, devices and tabs, Help), console (shell, log, search, filter, history, completion, batch, save view, console
   files), flashing (application and LTE modem firmware, catalog ID, UICR, erase / recover / reset, flash wizard),
   Bluetooth LE (scan, connect, pairing), troubleshooting (every `hwio::Error` code; doctor, diagnostic report),
   privacy (update check, diagnostic report, local crash dumps, what is stored where).
3. English reference pages: `cli.md` (`hwio`, every command and option, `--json`, exit codes), `cli-migration.md`
   (from py-hardwario's `hardwario`), `mcp.md` (`hwio mcp` stdio and `--http`, the GUI server, client setup for
   Claude Code / Claude Desktop / Cursor / VS Code, tools, consent, security).
4. Every command, option, path, UI text and error message checked against the code of the integrated build.

**Non-goals**
- Publishing to docs.hardwario.com; changes to the in-app links (`docsUrl()` stays, ADR-0220). No code changes.
- Screenshots (none in v2.0, ADR-0222).
- A `cli.md` generator or freshness CTest (dropped, ADR-0222); the `cli_compat` snapshot of ADR-0139 stays in 0017.
- Documentation of unfinished features (J-Link firmware update in the GUI, PIB/UICR write, catalog upload, modem
  trace, other devices).

## User experience

```
README.md                    product page: what it does, hardware, download, first run per OS, CLI examples,
                             documentation links, building from source, repository layout, licence
docs/README.md               language chooser (English · Čeština · Deutsch · Slovenčina · Polski)
docs/<lang>/index.md         contents of that language        <lang> = en, cs, de, sk, pl
docs/<lang>/install.md
docs/<lang>/getting-started.md
docs/<lang>/console.md
docs/<lang>/flashing.md
docs/<lang>/bluetooth.md
docs/<lang>/troubleshooting.md   anchor per error id, grouped by category / exit code
docs/<lang>/privacy.md
docs/en/cli.md               English only; linked from every language
docs/en/cli-migration.md
docs/en/mcp.md
```

Developer documentation stays where it is (`CLAUDE.md`, `plan/`, `packaging/README.md`, `packaging/RELEASE.md`);
the README links to it.

Tone and depth as tower-bridge: short sentences, UI names in **bold** exactly as the app shows them in that language,
keys in `<kbd>`, tables for options, `<details>` for per-OS steps in the README, no marketing.

## CLI

None (documentation only).

## Design

- **Repository URL:** README badges, release downloads and the issue tracker are the only absolute URLs to the
  repository; each file that needs one has a single reference definition `[repo]: https://github.com/hardwario/monitor`
  at its end. Everything else is a relative link.
- **Sources of truth for the text:** `hwio … --help` of the integrated build (`build/linux-debug/app/cli/hwio`);
  `libs/core/include/hwio/core/error.h` (codes, categories) and the factory functions that build each error's
  message / cause / suggestion; QML and `i18n/hwio_*.ts` for UI names; `packaging/` for package names, paths, udev
  rule; plans and ADRs only where the code confirms them.
- **Translations:** written from the English page after it is final; UI names from the language's `.ts` catalog
  (English string where plan 0030 has not translated it yet). Czech reviewed by the user; German, Slovak and Polish
  reviewed by native speakers later (open point).
- **No device identifiers:** no J-Link probe model names or serial numbers anywhere; examples use placeholders
  (`<serial>`) and a documentation MAC address such as `F1:22:33:44:55:66`.
- **Anchors:** GitHub turns `### probe.not_found` into `#probenot_found`; the troubleshooting pages use explicit
  `<a id="probe.not_found"></a>` anchors so the id is the anchor in every language (ready for a later `docsUrl()`).

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | Where the docs live | `docs/` in the repo (ADR-0138) / `docs/` + docs.hardwario.com now / docs.hardwario.com only | `docs/` in the repo; docs.hardwario.com later (2026-11-03) | ADR-0220 |
| D2 | Languages | EN only / manual EN + CS / EN, CS, DE, SK, PL | five languages (2026-11-03) | ADR-0221 |
| D2a | Which pages are translated, layout | user pages in 5, references EN / everything in 5 / EN in `docs/`, translations in `docs/<lang>/` | user pages in five languages under `docs/<lang>/`, `cli`, `cli-migration`, `mcp` English in `docs/en/` (2026-11-03) | ADR-0221 |
| D3 | Structure | tower-bridge model (one-file manual) / page per topic (ADR-0138) | page per topic (2026-11-03) | ADR-0222 |
| D4 | Screenshots | mock kit, light + dark / light only / none | none in v2.0 (2026-11-03) | ADR-0222 |
| D5 | `cli.md` | hand-written, checked against `--help` / generated + CTest | hand-written (2026-11-03) | ADR-0222 |
| D6 | README | product page as tower-bridge / short | product page (2026-11-03) | ADR-0222 |
| D7 | In-app links (`docsUrl()`) | unchanged / GitHub `docs/<lang>/` now | unchanged (2026-11-03) | ADR-0220 |
| D8 | New repository | — | `hardwario/monitor` (2026-11-03) | — |
| D9 | `troubleshooting.md` depth | every error code by category / common problems only | every code, grouped by category (2026-11-03) | ADR-0222 |
| D10 | Translation review | agent writes, natives review later / DE/SK/PL only after the EN review | agent writes all; CS by the user, DE/SK/PL natives later (2026-11-03) | ADR-0221 |

## Implementation steps

- [x] A1 ADRs 0220–0222; ADR-0138 status updated.
- [x] B1 `docs/en/*` (user pages and references), checked against the build and the code.
- [x] B2 `README.md`, `docs/README.md`.
- [x] B3 `docs/cs/*` (user reviews).
- [x] B4 `docs/de/*`, `docs/sk/*`, `docs/pl/*`.
- [x] C1 Accuracy pass (every option in `cli.md` against `--help` and back; every error id in `error.h` has an anchor
      in each `troubleshooting.md`); link and anchor check; no probe names / serials; `tools/check-lfs.sh`.
- [x] C2 Roadmap, open question 29, 0017 phase E (moved here, generator dropped) and `packaging/RELEASE.md` (docs
      check before tagging) updated; plan Done.

## Tests and verification

- Every command and option in `docs/en/cli.md` exists in `hwio … --help` of the integrated build, and vice versa.
- Every error id in `libs/core/include/hwio/core/error.h` has an anchor in each `docs/<lang>/troubleshooting.md`.
- All relative links and anchors resolve (script in the scratchpad, run before the commit).
- `tools/check-lfs.sh` passes; `grep` finds no probe model names or serial numbers.
- The user reviews the English and Czech pages.

## Verification record (2026-11-03)

- Options: every `--option` of `hwio … --help` (integrated build 770a51f) appears in `docs/en/cli.md`, `mcp.md` or
  `bluetooth.md`, and no documented option is missing from `--help`; every command of the help tree is in `cli.md`.
- Error ids: the 88 ids of `error.h` have an anchor in each of the five `troubleshooting.md`, none extra; the English
  and Czech texts were generated from `error.cpp` / `i18n/hwio_cs.ts`, the DE/SK/PL pages quote the English texts the
  app shows.
- Links: all relative links and anchors of `README.md` and `docs/**` resolve (script, 0 problems).
- `tools/check-lfs.sh` and `--untracked` pass (no images were added); no probe model names or serial numbers.
- Found while writing (not changed here): the GUI has no wizards, no Bluetooth and no MCP server yet (plans 0008–0010,
  0012, 0014), so the docs describe Bluetooth and MCP through `hwio` only; the update check
  (`libs/update/include/hwio/update/releases.h`) and Settings › About › Source code still point to
  `hardwario/hio-monitor`, which must follow the repository move; `internal.session_limit` says "max 8" also for the
  J-Link out-handler limit of 16.

## Risks and open points

- German, Slovak and Polish texts are not reviewed by native speakers yet.
- Hand-written references drift from the code; the release checklist gets a "docs match `--help`" item.
- The UI names in DE/SK/PL depend on plan 0030; if it changes strings later, the pages need a pass.
- The repository move to `hardwario/monitor` is assumed; if the name changes, one reference definition per file.

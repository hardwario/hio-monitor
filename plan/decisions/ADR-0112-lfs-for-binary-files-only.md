# ADR-0112 — Git LFS for binary files; text files stay in Git at any size

- **Status:** Accepted (amends the Git LFS part of ADR-0013)
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad

## Context

ADR-0013 and CLAUDE.md required Git LFS for binary files **and** for any file over 100 KB that is not source
text. `tools/check-lfs.sh` enforced 100 KB for every file, including Markdown. Plan 0002 grew to 103 KB with its
HIL notes and was split into two files only to satisfy the check. In LFS, a text file loses readable diffs,
blame and review.

## Decision

- **Binary files** go through Git LFS (patterns in `.gitattributes`), as before. Vendor SDK binaries are never
  committed (ADR-0006).
- **Text files stay in plain Git at any size**: sources, plans, ADRs, translations (`.ts`), docs, test fixtures
  written by hand.
- **Generated or compiled text** over 100 KB, such as Intel HEX firmware images (`*.hex`), linker maps
  (`*.map`) or S-records, still goes through Git LFS. `tools/check-lfs.sh` keeps the 100 KB limit only for
  these extensions.
- The split of plan 0002 into `0002-m1-jlink-rtt.md` + `0002-m1-jlink-rtt-reviews.md` stays as it is.

## Consequences

- `tools/check-lfs.sh` flags binary files that are not LFS pointers, and large generated text by extension.
- CLAUDE.md "Repository rules" is updated.

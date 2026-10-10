# ADR-0076 — Wizard illustrations: slot list now, placeholder art, real art later

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

The connect and flash wizards need pictures of the CHESTER connectors, the J-Link flat cable and power. No such
art exists in the old app or elsewhere. 0003-ux-and-wizards requires illustrations without embedded text, stored
via Git LFS.

## Options considered

1. **Define the asset slots; ship neutral placeholders; swap in real art later (chosen).**
2. The implementation agent draws schematic SVGs (needs reference photos and dimensions).
3. HARDWARIO provides photos before implementation (blocks the plan).

## Decision

- Assets live in `app/gui/assets/wizard/<slot-id>[-dark].svg` (or `.png` @2x), tracked by Git LFS
  (`.gitattributes` gains the directory pattern before the first commit).
- The slot list (id, what it shows, caption source text, size 480 × 270 logical px) is part of feature 0007 and
  extended by 0009 / 0010. Captions are separate translatable texts; images contain no text.
- A slot without a dark variant uses the light one on a `surface` background.
- The implementation ships placeholder SVGs (outline frame, the Material Symbol of the topic, the slot id in the
  file's `<title>` only). Replacing a file needs no code change. A Qt Quick Test checks that every slot in the table
  resolves to a file.

## Consequences

- Wizards are complete and testable before the art exists. Real art is a separate task for HARDWARIO.

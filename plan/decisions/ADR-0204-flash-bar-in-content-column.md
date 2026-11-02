# ADR-0204 — Flash bar content follows the centred form column; Flash button has the bolt icon

- **Status:** Accepted (plan [0028](../features/0028-gui-feedback-round-2.md), approved 2026-11-02); amends the
  sticky bar of ADR-0055 / ADR-0174 (layout only)
- **Date:** 2026-11-02
- **Decided by:** Matej Strnad (customer feedback, GUI feedback round 2)

## Context

The Flash page form is a centred column of at most 880 px (`FlashPage.qml`), but the sticky FlashBar spans the
whole window and puts the Flash button at the far right edge. On a large monitor the button is far from the form
and easy to miss.

## Options considered

1. Leave the bar as is.
2. Move the Flash button into the form (not sticky).
3. **Keep the bar sticky and full width (background and border), but lay its contents out in the same centred
   column as the form; add the bolt icon (`flash.svg`) to the Flash button (chosen).**

## Decision

- FlashBar background and top border stay full width and sticky at the bottom of the page (ADR-0055, ADR-0174).
- The bar's row (status icon, summary / reason, Ctrl+Enter hint, Flash button) is constrained to the form column's
  x and width, so the Flash button ends at the right edge of the form.
- The Flash button shows the `flash` icon before its label (`HButton.iconName`).
- The catalog ID row and the rest of the form are unchanged.

## Consequences

- The design system's FlashBar entry is updated; screenshots of the Flash page are retaken.

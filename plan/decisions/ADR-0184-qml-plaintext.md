# ADR-0184 — QML text bound to data is always PlainText

- **Status:** Accepted
- **Date:** 2026-10-29
- **Decided by:** Matej Strnad

## Context
Review M-2: Qt Quick `Text`/`Label` default to `Text.AutoText`; device strings such as `fw-name` containing HTML
switch a label to rich text, fetch `<img>` URLs and can spoof the UI.

## Options considered
1. Explicit `textFormat: Text.PlainText` on every data-bound label + an automated check.
2. New `HLabel`/`HText` components with PlainText default used everywhere.

## Decision
Option 1. Every `Label`/`Text`/`TextArea`/`TextField` whose `text` is not a literal or `qsTr()` of a literal sets
`textFormat: Text.PlainText`. CTest `qml_plaintext_check` enforces it; its allow-list contains only `WizardDialog.qml`,
whose bodies and `linkUrl` must stay constant translated strings (contract comment in `wizard.h`).

## Consequences
New QML that forgets the property fails CI. Rich text is possible only in the wizard.

## Amendment 2026-11-02
ToolTips are covered too: all tooltips use `HToolTip`, a styled ToolTip whose `contentItem` is PlainText, and
`qml_plaintext_check` also checks `ToolTip.text` / `ToolTip { text: }` (review round 1, R7).

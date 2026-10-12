# GUI design system

- **Status:** Draft (with features [0004](../features/0004-m3-gui-shell.md), [0005](../features/0005-m3-console.md), [0006](../features/0006-m3-flash-settings.md))
- **Related ADRs:** ADR-0043 (layout), ADR-0044 (Material + Theme), ADR-0045 (console views), ADR-0046 / ADR-0056 (search & filter), ADR-0055 (flash kinds),
  ADR-0047 (themes), ADR-0048 (fonts), ADR-0051 (shortcuts)
- **Mockups:** [`mockups/index.html`](mockups/index.html) (open in a browser; light/dark and EN/CS toggles)

All values live in the `Theme` QML singleton (`app/gui/qml/Theme.qml`). Components never hard-code colours, sizes
or fonts. The mockups' CSS custom properties use the same token names.

## 1. Colour tokens

Proposed palette (ADR-0047). The dark theme keeps the old app's accent and log colours for familiarity.

| Token | Light | Dark | Use |
|---|---|---|---|
| `bg` | `#FFFFFF` | `#1E1F26` | window, pages |
| `surface` | `#F5F6F8` | `#262833` | rail, device bar, toolbars, cards |
| `surfaceRaised` | `#FFFFFF` | `#2E3040` | popovers, dialogs, menus |
| `consoleBg` | `#FBFBFC` | `#191A20` | shell / log views |
| `border` | `#D5D9DF` | `#3A3D4D` | dividers, inputs |
| `text` | `#1B1F24` | `#E6E8EE` | primary text |
| `textMuted` | `#57606A` | `#9BA3B4` | secondary text, timestamps, line numbers |
| `accent` | `#0B5FCC` | `#79B8FF` | primary buttons, focus ring, links, selected rail item |
| `onAccent` | `#FFFFFF` | `#0B1A2E` | text on accent |
| `success` | `#1A7F37` | `#56D364` | connected dot, success card |
| `warning` | `#8A5A00` | `#FFAB70` | warning level, warnings |
| `error` | `#C4232E` | `#FF7B86` | error level, error panel accent, destructive buttons |
| `logDebug` | `#636C76` | `#8B93A6` | debug lines |
| `logInfo` | = `text` | = `text` | info lines |
| `shellEcho` | = `accent` | = `accent` | echoed commands (`> cmd`) |
| `selection` | `#CCE0FF` | `#264F78` | selected rows |
| `findMatch` | `#FFE08A` | `#6B5418` | find matches (text keeps `text` colour) |
| `findCurrent` | `#FFB224` | `#B07A10` | current match |
| `errorPanelBg` | `#FFF1F1` | `#3A2228` | inline error panel background |
| `warningPanelBg` | `#FFF6E0` | `#3A3020` | warning banner background |

**Contrast** (WCAG 2.1, computed; AA needs ≥ 4.5 : 1 for text):

| Foreground | on `bg` light | on `consoleBg` light | on `surface` light | on `bg` dark | on `consoleBg` dark | on `surface` dark |
|---|---|---|---|---|---|---|
| text | 16.6 | 16.0 | 15.3 | 13.4 | 14.2 | 12.0 |
| textMuted | 6.4 | 6.2 | 5.9 | 6.5 | 6.9 | 5.8 |
| accent | 6.0 | 5.8 | 5.5 | 7.9 | 8.4 | 7.1 |
| logDebug | 5.3 | 5.2 | 4.9 | 5.3 | 5.6 | 4.8 |
| warning | 5.9 | 5.7 | 5.5 | 8.8 | 9.3 | 7.9 |
| error | 5.8 | 5.6 | 5.4 | 6.6 | 7.0 | 5.9 |
| success | 5.1 | 4.9 | 4.7 | 8.5 | 9.0 | 7.6 |

`onAccent` on `accent`: 6.0 (light), 8.4 (dark). `text` on `findMatch`: 12.8 (light), 5.9 (dark).
Colour is never the only signal: log levels also show a level tag column (`D I W E`), the connection state has a
text label, and matches have an outline in high-contrast mode.

## 2. Typography

| Token | Font | Size / weight | Use |
|---|---|---|---|
| `fontUi` | Inter | 13 px / 400 | default UI text |
| `fontUiSmall` | Inter | 12 px / 400 | captions, badges, status bar |
| `fontUiStrong` | Inter | 13 px / 600 | buttons, labels, tab titles |
| `fontTitle` | Inter | 18 px / 600 | page titles, empty-state headlines |
| `fontMono` | JetBrains Mono | `console/fontSize` (default 10 pt ≈ 13 px) / 400 | shell, log, IDs, addresses |

UI sizes follow the system scale factor (Qt high-DPI); the console size is user-adjustable (8–24 pt).

## 3. Spacing, size, shape

- Spacing scale (px): `xs 4`, `s 8`, `m 12`, `l 16`, `xl 24`, `xxl 32`.
- Control height 32 (Material Dense), tool button 28, rail item 56 × 56 (icon + label), device tab height 36,
  device bar height 48, console row height = font line height + 2.
- Radius: `radiusS 4` (inputs, chips), `radiusM 6` (buttons, cards), `radiusL 10` (dialogs).
- Elevation: only popovers / dialogs / toasts (Material elevation 4–8); everything else is flat with borders.
- **Minimum window 960 × 600**, default 1280 × 800 (window geometry remembered in `ui/windowGeometry`).
  Below 1100 px width the rail collapses to icons only (labels in tooltips); below 1000 px the shell/log split
  switches to a stacked (vertical) split.

## 4. Icons

Material Symbols (Apache 2.0) as SVG, subset only what is used, stored in `app/gui/assets/icons/*.svg`
(text, not LFS). Rendered with the theme colour via `IconImage`/`color`. Every icon-only button has a tooltip
and `Accessible.name`.

## 5. Components

| Component | Spec |
|---|---|
| **DeviceTabBar** | Tabs `CHESTER app · 683012345` + state dot (grey idle, amber connecting, green connected, red error) + close ×; `+` at the end (disabled at 8 tabs with tooltip). Middle-click closes. Tab reorder by drag. |
| **NavRail** | Console, Flash (BLE later); bottom: Settings (gear), Help (?). Selected item: accent pill behind icon. Badges: busy spinner on the page that runs an operation (Flash while flashing; the console shows a "paused" marker row), red dot on error. |
| **DeviceBar** | Probe label (product + SN, or "Choose device"), target, state text ("Connected · 3.3 V", "Connecting…", "Disconnected — target not powered"), primary button Connect / Disconnect / Cancel, ⋯ Advanced popover (ADR-0052). |
| **Toolbar** | Left: page actions as tool buttons with text labels (icons + text at ≥ 1100 px, icons only below); right: Follow toggle, Pause, overflow menu. Same position and order on every page. |
| **ConsoleView** | Header (title "Shell" / "Log", line count, dropped-lines warning), list area (columns: optional time `HH:MM:SS.mmm`, level tag, text), "▼ N new lines" pill bottom-right when not following. |
| **ShellInput** | Monospace field with `>` prompt, Send button; Up/Down history, Ctrl+R opens history popup (filter-as-you-type, Enter picks, Esc closes). Disabled with reason when not connected / flashing. |
| **ConsoleSearchBar** | Bottom of the log pane (ADR-0056): segmented **Search \| Filter**, field with Aa / .\* toggles; Search: `3 / 128`, ↑ ↓, ×; Filter: "Showing X of Y lines", Clear filter; level chips D / I / W / E on the right; active filter shown as chip in Search mode. Invalid regex → field border `error` + tooltip/inline text. Shell variant: Search only, shown above the input on Ctrl+F. |
| **KindCards** | Flash step 1 (ADR-0055): three radio cards in a row (stacked below 1000 px): title, MCU, accepted files (mono). Selected: accent border + `accentSoft` fill. Prerequisite problems appear as an inline hint under the cards. |
| **FlashBar** | Sticky bottom bar of the Flash page: summary of kind + file, or the reason Flash is disabled; Ctrl+Enter hint; primary Flash button. |
| **ErrorPanel** | Inline card, left border 4 px `error`, icon, **headline** (`message`), "Probably:" `cause`, "What to do:" `suggestion`, buttons: primary action (Retry / Reconnect…), "Help me fix this" (if `wizard`; M3 shows it disabled with "Coming soon" until M4), "Details" expander (code, `detail`, monospace, Copy). Window height < 700 px: collapses to a one-line headline + primary action + expand chevron. |
| **WarningBanner** | One-line banner (warningPanelBg) with action and dismiss — e.g. "J-Link firmware is outdated" / "312 lines were dropped". |
| **Toast** | Bottom-centre, 4 s, success/info only, max 3 stacked, never for errors (UX 0003). |
| **PhaseProgress** | Horizontal stepper of phases (done ✓, current with spinner, pending), progress bar with %, bytes (`412 / 803 kB`), elapsed, Cancel. Indeterminate bar only when total is unknown. |
| **ConfirmDialog** | Title as question, body lists what will be lost (bulleted), consequence in bold, buttons: Cancel (default, Esc) + destructive button repeating the action ("Erase everything") in `error`. No "don't ask again". |
| **EmptyState** | Icon, headline, one sentence, primary next-step button (UX principle 1). |
| **ShortcutOverlay** | F1: modal list grouped by area (Tabs, Console, Flash, General). |

## 6. Interaction and accessibility rules

- Focus ring: 2 px `accent` outline on every focusable control, never removed. Tab order: tab bar → rail →
  device bar → toolbar → content → input.
- Every interactive item has `Accessible.name` / `Accessible.description` (translated); console rows expose
  their text; state changes (connected / error) are announced via `Accessible.announce` (Qt 6.8).
- Hover alone never reveals essential actions.
- Long-running actions show progress within 300 ms and are cancellable.
- Destructive actions: confirmation per ADR-0038 only; ordinary actions never ask.
- Animations ≤ 150 ms and disabled when the OS requests reduced motion.

## 7. Localization and text length

- All strings `qsTr()`; Czech is typically 15–30 % longer: buttons size to content, no fixed-width text
  containers, labels may wrap to two lines in cards, toolbar labels elide with tooltip.
- The mockups' CS toggle shows the Czech strings to check layout; final Czech texts come from Qt Linguist.
- Numbers, sizes and times via `Qt.locale(language)` (e.g. `1 204` vs `1,204`, `412 kB`).

# 0003 — UX and wizards

- **Status:** Draft (principles approved at bootstrap; screens need a feature plan + user review)

## Principles

1. **Show the next step.** Every empty state tells the user what to do ("No device attached —
   Connect device").
2. **Never a dead end.** Every error offers an action: retry, open the relevant wizard, copy
   diagnostic details, open documentation.
3. **Plain language first, technical detail on demand.** Error panel = headline + likely cause
   + what to do; "Details" expander with codes, DLL versions, logs.
4. **Long operations are visible and cancellable** (progress with phases, Cancel button).
5. **Safe by default.** Destructive operations (erase all, recover) need confirmation and say
   what will be lost.
6. **Keyboard friendly** for power users; shortcuts documented in the UI.
7. Light and dark theme; scalable layout; accessibility (focus order, contrast, screen-reader names).
8. **English and Czech** (ADR-0027): every text translatable, language selectable in settings (System /
   English / Čeština) and switched live; layouts tolerate longer Czech texts; numbers and dates follow
   the selected language.

## Visual design

Tokens, components and accessibility rules: [gui/design-system.md](gui/design-system.md); screen mockups:
[gui/mockups/index.html](gui/mockups/index.html); M3 screens are specified in [features/0004](features/0004-m3-gui-shell.md) (shell),
[0005](features/0005-m3-console.md) (console) and [0006](features/0006-m3-flash-settings.md) (flash, settings).

## Wizards (each gets its own feature plan)

M4 plans: [0007 framework + diagnostics](features/0007-m4-wizard-framework-diagnostics.md),
[0008 first-run setup](features/0008-m4-first-run-setup.md),
[0009 connect + troubleshooting](features/0009-m4-connect-and-troubleshooting.md),
[0010 flash](features/0010-m4-flash-wizard.md). Wizards open in a modal dialog (ADR-0066) driven by C++ step
machines (ADR-0067). The macOS Bluetooth permission step and the BLE pairing wizard move to M5.

| Wizard | Trigger | Steps (sketch) |
|---|---|---|
| First-run setup | first start, or J-Link missing at start (ADR-0071) | detect J-Link Software (and optional nRF Util, needed only for modem firmware) → guide download/install → Linux udev rule (ADR-0072) → test probe (macOS Bluetooth permission: M5) |
| Connect device | "Connect" or connection error | choose device (CHESTER) → choose connection (J-Link / BLE) → illustrated cabling ("connect the J-Link flat cable to connector …", "power the device") → live checks (probe found? target voltage? MCU detected?) → done |
| Flash firmware | Flash page "Guide me", FlashFirmware errors (ADR-0074) | device and probe → kind → cable + live check → file / catalog ID (no catalog browsing, open question 15) → confirm → progress → offer to open console |
| Troubleshooting | from an `hio::Error` with `wizardId` | targeted checks per error (USB cable, flat cable, power, other app holding the probe, outdated J-Link firmware) |
| BLE pairing (M5) | BLE connect failure / "probably unpaired" | explain pairing/passkey, OS-specific steps, macOS Bluetooth permission |

Illustrations and photos are stored under `app/gui/assets/` via **Git LFS**. They contain no embedded
text (text goes into translatable captions), so they work in every language.

## Error presentation

- Inline error panel in the affected page (not only a short toast).
- Toasts only for transient success/info.
- "Create diagnostic report" (Help menu, `hio diag report`; ADR-0075) saves a zip with a summary (app version,
  OS, Qt, J-Link / nRF Util versions, probes, `hio doctor` results), the recent app logs and the settings file,
  with the home folder, user and host names redacted — for support tickets. Nothing is sent automatically.

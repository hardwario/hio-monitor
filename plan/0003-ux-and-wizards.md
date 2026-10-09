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

## Wizards (each gets its own feature plan)

| Wizard | Trigger | Steps (sketch) |
|---|---|---|
| First-run setup | first start, or missing dependency | detect J-Link Software (and optional nRF Util, needed only for modem firmware) → guide download/install → Linux udev rules / group membership → macOS Bluetooth permission → test probe |
| Connect device | "Connect" or connection error | choose device (CHESTER) → choose connection (J-Link / BLE) → illustrated cabling ("connect the J-Link flat cable to connector …", "power the device") → live checks (probe found? target voltage? MCU detected?) → done |
| Flash firmware | Flash page | pick source (file / catalog ID / browse catalog) → pick target MCU (app / LTE) → confirm → progress → verify → offer to open console |
| Troubleshooting | from an `hio::Error` with `wizardId` | targeted checks per error (USB cable, flat cable, power, other app holding the probe, outdated J-Link firmware) |
| BLE pairing | BLE connect failure / "probably unpaired" | explain pairing/passkey, OS-specific steps |

Illustrations and photos are stored under `app/gui/assets/` via **Git LFS**.

## Error presentation

- Inline error panel in the affected page (not only a short toast).
- Toasts only for transient success/info.
- "Copy diagnostic report" collects app version, OS, Qt, J-Link / nRF Util versions, probe SN,
  recent app log — for support tickets.

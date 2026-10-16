# Wizard illustrations (ADR-0076)

Illustration slots of the wizards (plan/features/0007 § Illustration slots; 0008–0010 add their own). Each slot is a
file `<slot-id>.svg` (or `.png` at 2×) of **480 × 270 logical px**, with an optional dark variant
`<slot-id>-dark.svg`; a slot without a dark variant is shown on the `surface` background. Images contain **no text**:
the caption is a separate translatable text. The files are stored with **Git LFS** (`.gitattributes`:
`app/gui/assets/wizard/*.svg`, `*.png`) and compiled into the QML module `HardwarioMonitor`
(`qrc:/qt/qml/HardwarioMonitor/assets/wizard/`). Replacing a file needs no code change.

The current files are **placeholders**: a dashed outline frame and the Material Symbol of the topic (paths from the
icons in `../icons`, Apache License 2.0, see `../icons/README.md`), the slot id only in the SVG `<title>`. The real art
is a content task for HARDWARIO (ADR-0076, open question 22 for the CHESTER connectors).

| Slot | Shows | Caption source text | Placeholder symbol |
|---|---|---|---|
| `jlink-usb` | J-Link connected to a computer by USB | "Connect the J-Link to your computer with a USB cable." | `probe` |
| `chester-app-cable` | flat cable to the application MCU's programming connector | from the profile guide (`TargetProfile::connectionGuide`, ADR-0077) | `device` |
| `chester-lte-cable` | flat cable to the LTE MCU's programming connector | from the profile guide | `device` |
| `chester-power` | CHESTER with batteries / USB-C power | from the profile power hint (`DeviceProfile::powerHintSourceText`) | `flash` |
| `placeholder` | neutral frame (fallback for an unknown slot) | — | `placeholder` |

A Qt Quick Test (`tst_wizard.qml`) and a GTest (`WizardIllustrations`) check that every slot of this table and every
illustration slot of a device profile's connection guide resolves to a file.

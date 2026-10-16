# ADR-0084 — BLE pairing: OS dialog, app-initiated pairing, BlePairing wizard, passkey hint

- **Status:** Accepted
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

CHESTER's NUS shell requires a bonded (encrypted) link with passkey entry. The passkey is stored in the PIB v2
(`ble_passkey`, ≤ 16 alphanumerics; py-hardwario `--ble-passkey`, default `123456`). The legacy app had no pairing
code: it relied on the OS dialog, treated any service error as "probably unpaired" and told users to pair in the
system Bluetooth settings. Qt 6 offers `QBluetoothLocalDevice::requestPairing` on Linux (BlueZ, with the desktop's
pairing agent) and Windows, but not on macOS, where pairing is triggered by CoreBluetooth on the first encrypted
access.

## Options considered

1. OS pairing on connect + guided wizard, no in-app pairing.
2. **OS pairing on connect, plus app-initiated pairing where Qt supports it, plus the wizard (chosen).**
3. Only an error message, no wizard.

Passkey hint: **default 123456, changeable via the PIB (chosen)** / "see the device label" / read from the PIB only.

## Decision

- Connecting enables TX notifications; on an unbonded link the OS starts pairing and shows its passkey dialog.
  The connect deadline is extended to **60 s** while the CCCD write is pending, so users have time to type the
  passkey.
- A CCCD or RX write failing with insufficient authentication/encryption (or the platform's equivalent) →
  `ble.not_paired` (wizard BlePairing). Other service errors are **not** reported as unpaired
  (`ble.service_not_found`, `ble.write_failed`).
- **App-initiated pairing:** `BleSession::pair()` / `hio ble pair -a ID` call `requestPairing` on Linux and
  Windows. On macOS they return `ble.pairing_not_supported` ("macOS pairs automatically when connecting") and the
  wizard shows the macOS path instead.
- **BlePairing wizard** (feature 0012): explains pairing, shows OS-specific steps, offers [Pair] where supported,
  shows the passkey hint: "The passkey is 123456 unless it was changed when the device was configured
  (`hio chester app pib write --ble-passkey`)." When a J-Link tab for a CHESTER is connected, it offers
  [Read passkey from the device] (PIB read, M2), shown only after the user clicks it.
- The app never stores passkeys and never types them into OS dialogs.

## Consequences

- Linux pairing needs a running BlueZ agent (desktop environments provide one); headless Linux falls back to the
  `bluetoothctl` commands shown by the wizard and `hio ble pair` (risk listed in 0011).
- Windows pairing UI behaviour differs between versions; verified in HIL.

# ADR-0089 — BLE facts live in `DeviceProfile::ble`

- **Status:** Accepted (extends ADR-0032 and ADR-0077)
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

Generic code must not hard-code CHESTER values (ADR-0009). BLE needs per-device facts: advertised name filter,
service and characteristic UUIDs, shell prompt, passkey hint and pairing guide texts. `TargetProfile` is J-Link/RTT
shaped (`jlinkDevice`, RTT buffer names, speeds).

## Options considered

1. **An optional `BleProfile` block on `DeviceProfile` (chosen).**
2. A BLE "target" in `TargetProfile` with transport-specific fields.
3. A separate BLE profile registry.

## Decision

```cpp
struct BleProfile {
    std::string_view namePattern;          // substring of the advertised name; CHESTER: "CHESTER"
    std::string_view serviceUuid;          // NUS 6e400001-b5a3-f393-e0a9-e50e24dcca9e
    std::string_view rxUuid, txUuid;       // …0002… (write), …0003… (notify)
    std::string_view shellPrompt;          // "bt_nus:~$ "
    std::string_view consoleTargetId;      // "app": history / console files keyed like the J-Link app console
    const char* passkeyHintSourceText;     // QT_TRANSLATE_NOOP("hio::device", "The passkey is 123456 unless …")
    std::span<const ConnectionGuideStep> pairingGuide;   // ADR-0077 step type; illustration slots ble-*
};
// DeviceProfile gains:
std::optional<BleProfile> ble;            // devices without BLE leave it empty
```

- `hashPrefixIsLog` and level routing come from the target named by `consoleTargetId`.
- `ProfileRegistry::findByAdvertisement(name, serviceUuids)` returns the matching profile for scan rows.

## Consequences

- A new BLE device = a new profile entry; scanner, session, CLI and wizard do not change.
- History file `history-chester-app.txt` is shared by the J-Link and BLE consoles of CHESTER (same shell).

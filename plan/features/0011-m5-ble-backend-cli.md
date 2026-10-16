# 0011 — M5 BLE backend and CLI: `libs/hw/ble`, `ConsoleSession`, `BleSession`, `hio ble …`

- **Status:** Draft
- **Milestone:** M5 (plan 1 of 2, ADR-0078; followed by [0012](0012-m5-ble-gui-pairing.md))
- **Author:** Matej Strnad (with Claude), 2026-10-10
- **Related ADRs:** ADR-0002, ADR-0004, ADR-0007, ADR-0009, ADR-0012, ADR-0020, ADR-0022, ADR-0024, ADR-0026,
  ADR-0027, ADR-0031, ADR-0032, ADR-0033, ADR-0053, ADR-0058, ADR-0063, ADR-0068, ADR-0070, ADR-0077,
  ADR-0078–ADR-0089
- **Depends on:** 0002 (M1: `DeviceSession`, `ConsoleLog`, `LineAssembler`, CLI console). The `bluetooth` doctor
  check (phase E) needs [0007](0007-m4-wizard-framework-diagnostics.md) phase A (`libs/diag`). Phases A–D can start
  right after M1.

## Context

v2.0 must replace the old app's Bluetooth shell (ADR-0009, parity item in 0002-legacy-inventory: "BLE scan (CHESTER
filter, RSSI sort, search), connect, NUS shell"). py-hardwario has no BLE; the old app (branch `main`:
`src/btworker.cpp`, `src/bluetooth.cpp`, `src/btdevicemodel.cpp`, `views/BluetoothWelcome.qml`) is the behavioural
reference:

- Nordic UART Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, RX (write) `6e400002-…`, TX (notify) `6e400003-…`;
- scan 5 s with `LowEnergyMethod`, keep devices whose name contains "CHESTER", sort by RSSI;
- connect → discover services → discover details → write `0x0100` to the TX CCCD → "connected";
- shell only (Zephyr shell over BT NUS, prompt `bt_nus:~$ `); "Device's log is not supported via Bluetooth";
- pairing by the OS dialog; any service error shown as "probably unpaired".

Its defects must not recur (0002-legacy-inventory): send reported before the write; unbounded message buffer;
null dereference in the discovery error handler; devices de-duplicated by name; write type chosen with `==` instead
of a bit test; MTU ignored and no chunking; no reconnect; controller deleted twice; a worker that ran on the GUI
thread.

M1 built `DeviceSession` directly on J-Link + RTT, so there is no transport seam. This plan extracts a
`ConsoleSession` interface (ADR-0079), adds the BLE stack on one event-loop thread (ADR-0080), the CHESTER BLE
profile (ADR-0089), the CLI and the doctor check. The GUI and the pairing wizard are 0012.

## Goals / Non-goals

**Goals**
1. `libs/hw/ble` (QtCore + QtBluetooth): `IBleApi`, `QtBleApi`, `FakeBleApi` (test builds only, ADR-0087),
   `BleHost` thread (ADR-0080), `BleScanner`, `NusLink` (ADR-0082), adapter/permission state, app-initiated pairing
   (ADR-0084).
2. `libs/device`: `ConsoleSession` interface (ADR-0079) implemented by `DeviceSession` and the new `BleSession`;
   `BleLock`; `SessionRegistry` keyed by J-Link SN or BLE id (ADR-0083); `DeviceProfile::ble` + CHESTER BLE profile
   (ADR-0089).
3. CLI: `hio ble scan|console|command|pair` (ADR-0085), generalised console UI.
4. `libs/diag`: `bluetooth` check in `hio doctor` (ADR-0088).
5. macOS: Info.plist with the Bluetooth usage description for the GUI bundle and the CLI (ADR-0086).
6. Every error an `hio::Error` with EN + CS texts; all queues and buffers bounded.

**Non-goals**
- GUI (picker, BLE tabs, console page binding, BlePairing wizard, setup step) — 0012.
- Logs over BLE (firmware does not send them; routing still works if it ever does, ADR-0082).
- Flashing, DFU or MCUmgr over BLE; BLE on devices other than CHESTER; BLE peripheral role.
- MCP tools for BLE (M6 reuses `ConsoleSession`).
- Flatpak Bluetooth permissions (M7, open question 7).

## User experience

No GUI changes. The CLI:

```
$ hio ble scan
ID                   NAME              RSSI   PROFILE   IN USE
F1:22:33:44:55:66    CHESTER-ABC123    -58    chester
C0:11:22:33:44:55    CHESTER-XYZ789    -81    chester   hio-monitor, PID 4242
2 devices (5 s scan). Use --all to show every Bluetooth device.

$ hio ble console --name ABC123
Connecting to CHESTER-ABC123 (F1:22:33:44:55:66)…
-- connected (Bluetooth, MTU 247) --
bt_nus:~$ version                                  ← replxx prompt at the bottom (ADR-0033)
Firmware version: v3.1.0
-- disconnected: the device is out of range or turned off --
^D
```

On first connect to an unpaired device the OS shows its passkey dialog; the CLI prints
"Pairing: enter the passkey in the system dialog (default 123456)." when no data arrives within 2 s of the CCCD
write. Keys as M1: Enter, Up/Down, Ctrl+R, **F4** reconnect, Ctrl+D / Ctrl+Q / Ctrl+C on an empty line quit. No
reset key (BLE sessions have no Reset capability).

The CLI is English only (ADR-0027).

## CLI

| Command | Behaviour |
|---|---|
| `hio ble scan [--timeout S=5] [--all] [--profile ID=chester]` | One scan of `S` seconds (1–60). Rows matching the profile (ADR-0083) or all with `--all`, sorted by RSSI. Human: table as above. `--json`: `data = {"adapter":{"name","address"}, "devices":[{"id","name","rssi","nusAdvertised","profile":"chester"\|null,"inUseBy":{"app","pid"}\|null}]}`. No device → empty list, exit 0. Adapter or permission problem → error, exit 4. |
| `hio ble console (-a/--address ID \| --name N) [--profile ID] [--auto-reconnect] [--history-file P] [--console-file P]` | Interactive console (M1 UI). `--name N`: 5 s scan, case-insensitive substring of the advertised name among profile matches; exactly one match required (else `ble.device_not_found` or `usage.invalid_argument` listing the ids). `--json`: the M1 NDJSON stream (`line`, `connection`, `dropped`, `warning`) plus `{"type":"connection",…,"transport":"ble","mtu":247}`. |
| `hio ble command CMD (-a ID \| --name N) [-t/--timeout S=3.0] [--console-file P]` | Connect, send each line of `CMD`, collect shell output until the prompt is seen after the last line or the **absolute** timeout (from the first send), disconnect. Human: output lines; `--json`: `data = {"command","output":[…],"log":[…]}` (M1 shape). Connect failure → that error's exit code. |
| `hio ble pair (-a ID \| --name N)` | App-initiated pairing (ADR-0084). Human: "Paired." / "Already paired."; `--json`: `data = {"id","paired":true}`. macOS → `ble.pairing_not_supported` (exit 2). |

Exit codes per ADR-0024 (no new categories): Bluetooth adapter / stack / permission → 4 (dependency); BLE device
problems → 6 (target); `ble.in_use_by_other_process` → 5 (probe, same meaning "connection busy" as J-Link);
`ble.pairing_not_supported` → 2 (usage). 0006-cli's exit-code meanings are extended in the text ("… or Bluetooth
adapter", "… or BLE device").

## Design

### Modules

```
libs/hw/ble/      hio_hw_ble (QtCore, Qt6::Bluetooth)  include/hio/hw/ble/*.h  src/*  src/fake/* (HIO_TEST_FAKES)  tests/*
libs/device/      + console_session.h, ble_session.{h,cpp}, src/ble_worker.cpp, ble_lock.{h,cpp}, session_key.h;
                    profile.h (+BleProfile), profiles/chester.cpp (+BLE block)
libs/diag/        + bluetooth check (environment.cpp), EnvironmentContext::ble
libs/core/        + error codes, lcBle ("hio.ble")
app/cli/src/      ble_commands.{h,cpp}; console_ui / console_events / chester_commands take ConsoleSession&
app/cli/macos/Info.plist.in, app/gui/macos/Info.plist.in
tests/ble_scenarios/*.json   fake scenarios shared by unit, CLI e2e and integration tests
```

Top-level `find_package(Qt6 6.8 REQUIRED COMPONENTS Core Bluetooth LinguistTools)`. `hio_hw_ble` is created with
`hio_add_backend_library` (static, ADR-0022; the ADR-0004 link check stays: QtBluetooth is allowed, GUI modules
are not). Qt Bluetooth is LGPLv3, dynamically linked (ADR-0002).

### `libs/hw/ble` (`hio::ble`)

```cpp
using BleDeviceId = std::string;        // "F1:22:33:44:55:66" (Linux/Windows) or CoreBluetooth UUID (macOS), ADR-0083

enum class AdapterState { Ready, NotFound, PoweredOff, PermissionDenied, PermissionUndetermined, StackUnavailable };
struct AdapterInfo { AdapterState state; std::string name; std::string address; };

struct Advertisement { BleDeviceId id; std::string name; int rssi; std::vector<std::string> serviceUuids;
                       std::chrono::steady_clock::time_point seen; };

enum class WriteMode { WithResponse, WithoutResponse };
struct LinkReady { std::uint16_t mtu; WriteMode writeMode; };
using LinkId = std::uint32_t;

class IBleListener {                    // called on the BLE thread only, never throws
public:
    virtual ~IBleListener() = default;
    virtual void onAdvertisement(const Advertisement&) = 0;
    virtual void onScanFinished(std::optional<hio::Error>) = 0;
    virtual void onLinkConnecting(LinkId, bool waitingForPairing) = 0;
    virtual void onLinkReady(LinkId, LinkReady) = 0;
    virtual void onLinkData(LinkId, std::span<const std::byte>) = 0;
    virtual void onWriteDone(LinkId, std::uint64_t token, std::optional<hio::Error>) = 0;
    virtual void onLinkLost(LinkId, hio::Error) = 0;
    virtual void onPairingFinished(BleDeviceId, Status) = 0;
};

class IBleApi {                         // BLE thread only; implemented by QtBleApi and FakeBleApi; GMock'd
public:
    virtual ~IBleApi() = default;
    virtual void setListener(IBleListener*) = 0;
    [[nodiscard]] virtual AdapterInfo adapter() = 0;
    [[nodiscard]] virtual Status startScan(std::chrono::milliseconds duration) = 0;   // 0 = until stopScan
    virtual void stopScan() = 0;
    // connect → service discovery → characteristic discovery → CCCD enable → MTU; → onLinkReady or onLinkLost
    [[nodiscard]] virtual Result<LinkId> connect(const BleDeviceId&, const NusUuids&) = 0;
    [[nodiscard]] virtual Status write(LinkId, std::span<const std::byte> chunk, WriteMode, std::uint64_t token) = 0;
    virtual void disconnect(LinkId) = 0;   // idempotent; no onLinkLost afterwards
    [[nodiscard]] virtual Status requestPairing(const BleDeviceId&) = 0;   // macOS: ble.pairing_not_supported
};
[[nodiscard]] std::unique_ptr<IBleApi> createBleApi();   // QtBleApi, or FakeBleApi when HIO_TEST_FAKES and
                                                          // HIO_FAKE_BLE_SCENARIO is set (ADR-0087)
```

- **`QtBleApi`** — the only code touching QtBluetooth. `QBluetoothDeviceDiscoveryAgent` (LowEnergyMethod),
  `QBluetoothLocalDevice` (adapter, pairing), per link one `QLowEnergyController::createCentral` + `QLowEnergyService`,
  all children of the `BleWorker` (Qt parent ownership, deleted with `deleteLater` on the BLE thread, never
  `delete`d twice). Address type from `QBluetoothDeviceInfo`, not hard-coded. Write mode by **bit test** of the RX
  properties. MTU from `QLowEnergyController::mtu()` (fallback 23). Error mapping:
  - controller `ConnectionError`/`UnknownRemoteDeviceError` → `ble.connect_failed`;
  - NUS service or characteristics missing → `ble.service_not_found`;
  - CCCD/RX write rejected with insufficient authentication/encryption (BlueZ `org.bluez.Error.NotPermitted`/
    `NotAuthorized`, CoreBluetooth `CBATTErrorInsufficientAuthentication`/`Encryption`, WinRT
    `GattCommunicationStatus::AccessDenied`/`ProtocolError` with ATT 0x05/0x0F; mapped from Qt's error plus the
    platform detail where Qt exposes it) → `ble.not_paired`;
  - other descriptor/characteristic write errors → `ble.write_failed`;
  - remote disconnect → `ble.disconnected`.
  The discovery-agent error handler only uses the agent (legacy null-deref bug).
- **`BleHost`** (ADR-0080) — owns the `QThread` and the `BleWorker`; `post(BleCommand)` into a `BoundedQueue`
  (256, Reject → `internal.queue_full`); events out through a `BoundedQueue<BleEvent>` (10 000, DropOldest +
  counter) per subscriber (scanner, each session), each with a coalesced queued wake-up. `stop()` disconnects all
  links, deletes the QtBluetooth objects on the BLE thread, quits, joins (3 s bound).
- **`BleScanner`** (façade, owner thread) — `start(ScanOptions{duration, profileFilter, all})`, `stop()`, signal
  `devicesChanged(QList<BleDeviceInfo>)` batched ≤ 4/s; `BleDeviceInfo { BleDeviceId id; std::string name; int rssi;
  bool nusAdvertised; const DeviceProfile* profile; steady_clock::time_point lastSeen; }`; de-duplicated by `id`,
  max **256** devices (oldest by `lastSeen` evicted), names truncated at 64 bytes. Used by the CLI and by 0012.
- **`NusLink`** (BLE thread, platform-neutral, on `IBleApi`) — ADR-0082: line → chunks of `mtu − 3`; with-response:
  one chunk in flight; without-response: ≤ 16 queued; per-line completion token; 2 s write timeout →
  `ble.write_timeout` + link down; pairing wait (CCCD pending) up to 60 s with a `PairingPrompt` event after 2 s.

### `libs/device` (`hio::device`)

**`ConsoleSession`** (ADR-0079):

```cpp
enum class Transport { JLink, Ble };
struct SessionIdentity { Transport transport; std::string id /*SN or BleDeviceId*/; std::string displayName; };
enum class Capability : std::uint8_t { Reset = 1, LogChannel = 2, Operations = 4 };

class ConsoleSession : public QObject {
    Q_OBJECT
public:
    [[nodiscard]] virtual Status sendLine(std::string line) = 0;     // queued; error only if queue full/closed
    [[nodiscard]] virtual Status reconnect() = 0;
    virtual void close() = 0;                                         // stop, idempotent
    [[nodiscard]] virtual const ConsoleLog& log() const = 0;          // owner thread only (ADR-0063)
    [[nodiscard]] virtual ConnectionEvent connection() const = 0;
    [[nodiscard]] virtual SessionIdentity identity() const = 0;
    [[nodiscard]] virtual QFlags<Capability> capabilities() const = 0;
    [[nodiscard]] virtual const DeviceProfile& profile() const = 0;
    [[nodiscard]] virtual std::string_view consoleTargetId() const = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> consoleFilePath() const = 0;
signals:
    void linesReceived(const QList<hio::device::ConsoleLine>&);
    void connectionChanged(const hio::device::ConnectionEvent&);
    void linesDropped(quint64 count);
    void warning(const hio::Error&);
    void promptSeen();                                                // BLE only (ADR-0082); never for J-Link
};
[[nodiscard]] Status waitUntilConnected(ConsoleSession&, std::chrono::milliseconds, std::stop_token = {});
```

`DeviceSession` derives from it (identity = `{JLink, sn, "J-Link <sn>"}`, capabilities = all). Its remaining API is
unchanged.

**`BleSession`**:

```cpp
struct BleSessionOptions {
    const DeviceProfile* profile;          // must have profile->ble
    BleDeviceId deviceId; std::string deviceName;   // name for display/logs; may be empty
    bool autoReconnect = false;
    std::optional<std::filesystem::path> consoleFile;   // nullopt = default, empty = disabled (M1 rules)
    std::shared_ptr<ConsoleLog> consoleLog;             // ADR-0063
};
class BleSession final : public ConsoleSession {
public:
    // Non-blocking like ADR-0053: validate, take registry slot + BleLock, post Connect to BleHost, return.
    [[nodiscard]] static Result<std::unique_ptr<BleSession>> open(SessionRegistry&, BleHost&, BleSessionOptions);
    [[nodiscard]] Status pair();                      // ADR-0084; result as warning/connectionChanged
    [[nodiscard]] std::optional<int> rssi() const;    // last seen
    [[nodiscard]] std::uint16_t mtu() const;          // 0 until Connected
signals:
    void pairingPrompt();                             // CCCD pending > 2 s: "enter the passkey in the system dialog"
    void pairingFinished(const hio::Status&);
};
```

- Data path: NUS bytes → `LineAssembler` (target `consoleTargetId`'s rules) on the **owner thread** (BLE thread
  stays light, ADR-0080) → `ConsoleLog` + `ConsoleFileSink` (file name `<profile>-ble-YYYYMMDD-HHMMSS-<id>.log`,
  M1 rotation/retention, connection lines ` @ `) + batched signals. Prompt detection per ADR-0082.
- Reconnect (ADR-0082): with `autoReconnect`, after `Disconnected` the session asks the host for a background scan
  and retries every 3 s while the device was seen in the last 15 s; `reconnect()` always retries at once.
  `close()` during connecting cancels within one BLE-thread turn.
- **`BleLock`** — as `ProbeLock` (ADR-0058, ADR-0083), `locks/ble-<sanitised id>.lock`.
- **`SessionRegistry`** — `SessionKey = std::variant<ProbeKey, BleKey>`; `checkAvailable(SessionKey)`; max 8
  sessions total; `bleInUse(id)` → this-process tab/session or other-process lock owner (for scan rows).
- **Profiles** — `BleProfile` and `DeviceProfile::ble` per ADR-0089; CHESTER: name `CHESTER`, NUS UUIDs, prompt
  `bt_nus:~$ `, console target `app`, passkey hint, pairing guide steps (slots `ble-enable`, `ble-pair-os`; 0012
  draws them). `ProfileRegistry::findByAdvertisement`.

### `libs/diag`

`bluetooth` check per ADR-0088 (after `app-dirs`; 5 s; Warning only). `EnvironmentContext` gains
`std::shared_ptr<ble::BleHost> ble` (nullptr → the check creates a short-lived host). The check reads
`IBleApi::adapter()` only; it never scans (no macOS prompt).

### macOS (ADR-0086)

`app/gui/macos/Info.plist.in` (`MACOSX_BUNDLE_INFO_PLIST`, existing keys + `NSBluetoothAlwaysUsageDescription`);
`app/cli/macos/Info.plist.in` linked into `hio` with `-Wl,-sectcreate,__TEXT,__info_plist,<file>`. CTest on macOS
verifies both.

### Error codes added (texts translatable, ADR-0027)

| Code | Category | Message / cause / suggestion | Wizard |
|---|---|---|---|
| `ble.adapter_not_found` | Dependency | "No Bluetooth adapter was found." / the computer has no Bluetooth or it is disabled in the system / "Enable Bluetooth or connect a USB Bluetooth adapter." | BlePairing |
| `ble.adapter_off` | Dependency | "Bluetooth is turned off." / — / "Turn Bluetooth on in the system settings." | BlePairing |
| `ble.permission_denied` | Dependency | "HARDWARIO Monitor is not allowed to use Bluetooth." / denied in Privacy settings (macOS) / "Allow Bluetooth for HARDWARIO Monitor (or your terminal) in System Settings → Privacy & Security → Bluetooth." | FirstRunSetup |
| `ble.stack_unavailable` | Dependency | "The Bluetooth service of the system is not available." / BlueZ not running (Linux) / "Start the Bluetooth service: `sudo systemctl start bluetooth`." | BlePairing |
| `ble.device_not_found` | Target | "The Bluetooth device %1 was not found." / out of range, turned off, Bluetooth disabled in the device / "Bring the device closer and make sure its Bluetooth is on." | BlePairing |
| `ble.connect_failed` | Target | "Could not connect to %1 over Bluetooth." / weak signal, device busy with another connection / "Move closer, disconnect other phones or computers from the device and try again." | BlePairing |
| `ble.service_not_found` | Target | "The device does not offer the console service (NUS)." / firmware without the Bluetooth shell / "Use firmware with the Bluetooth console enabled." | BlePairing |
| `ble.not_paired` | Target | "The device is not paired with this computer." / pairing was cancelled, failed or was removed on one side / "Pair the device: enter its passkey when the system asks." | BlePairing |
| `ble.pairing_failed` | Target | "Pairing with %1 failed." / wrong passkey, timeout, stale bond on one side / "Remove the device in the system Bluetooth settings and pair again." | BlePairing |
| `ble.pairing_not_supported` | Usage | "This system pairs Bluetooth devices only when connecting." / macOS / "Connect to the device; macOS asks for the passkey." | — |
| `ble.write_failed` | Target | "Sending to the device over Bluetooth failed." / link lost during the write / "Reconnect." | BlePairing |
| `ble.write_timeout` | Target | "The device is not reading commands." / firmware busy or link stalled / "Reconnect; if it repeats, restart the device." | BlePairing |
| `ble.disconnected` | Target | "The Bluetooth connection to %1 was lost." / out of range, device restarted or turned off / "Bring the device closer; the console reconnects automatically if enabled." | BlePairing |
| `ble.in_use_by_other_process` | Probe | "The device %1 is used by another program (%2, PID %3)." / another HARDWARIO Monitor window or `hio` command / "Close the other program." | — |

### Threading summary

Owner thread (GUI / CLI main): `BleScanner`, `BleSession` façades, `LineAssembler`, `ConsoleLog`,
`ConsoleFileSink`, `CommandHistory`, registry, locks. BLE thread (one per application, ADR-0080): `BleWorker`,
`IBleApi` implementation, all QtBluetooth objects, `NusLink`s. Only the bounded queues cross threads.

### Bounds

BLE command queue 256 · event queue 10 000 per subscriber (DropOldest + `LinesDropped`) · scan list 256 devices,
name 64 B · outgoing line 4 KiB · chunks in flight 1 (with-response) / 16 (without) · partial line 4 KiB ·
ConsoleLog 100 000 lines/channel · console files and history as M1 · sessions 8 total · timeouts: scan default 5 s
(1–60), connect 20 s (60 s while pairing), write 2 s, reconnect 3 s while seen in 15 s, `ble command` 3 s,
`BleHost::stop` 3 s, doctor check 5 s, scanner signal ≤ 4/s.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| M5 split | two / three / one | two: backend + CLI (0011), GUI + wizard (0012) | ADR-0078 |
| Session API | common `ConsoleSession` / transport inside DeviceSession / separate | common `ConsoleSession` interface | ADR-0079 |
| BLE threading | one shared BLE QThread / per session / owner thread | one shared BLE QThread | ADR-0080 |
| NUS content | shell only / shell + logs / both | follow the old app: shell only, routing rules kept | ADR-0082 |
| Scan filter | profile name + NUS UUID / name only / UUID only | profile name pattern or NUS UUID, `--all` | ADR-0083 |
| Identity, lock, restore | address/UUID + lock + restore / no lock / no restore | address/UUID + `BleLock` + tab restore | ADR-0083 |
| Pairing | OS + wizard / + app-initiated / error only | OS + app-initiated (Linux/Windows) + wizard | ADR-0084 |
| Passkey hint | default 123456 via PIB / label / PIB only | default 123456, changeable via PIB | ADR-0084 |
| Reconnect | as J-Link option / always / never | same option as J-Link | ADR-0082 |
| CLI commands | scan+console / +command / +command+pair | scan, console, command, pair | ADR-0085 |
| macOS CLI permission | embedded plist / unsupported / CLI in bundle | embedded Info.plist | ADR-0086 |
| Test seam | `IBleApi` + fake (test builds) / always compiled / GMock only | `IBleApi` + fake, `HIO_TEST_FAKES` only | ADR-0087 |
| Bluetooth checks | doctor / setup step / pairing wizard only | doctor + setup step | ADR-0088 |
| Profile shape | `DeviceProfile::ble` / BLE target / separate registry | `DeviceProfile::ble` | ADR-0089 |
| Minor defaults: all bounds and timeouts above; error codes and categories (adapter → dependency, device → target, busy → probe); `lcBle` = `hio.ble`; console file name `<profile>-ble-…`; `--name` substring match; supported stacks = BlueZ of Ubuntu 22.04 (5.64) or newer, Windows 10 22H2+, macOS 13+ (ADR-0012) | accept / change | _pending_ | — |

## Implementation steps

Phase A — BLE library
- [ ] A0 Prerequisite check: M1 (0002) implemented (otherwise stop and report)
- [ ] A1 CMake: `Qt6::Bluetooth`, `hio_hw_ble` target, `HIO_TEST_FAKES` option + release-preset guard; `lcBle`
- [ ] A2 `IBleApi`, `IBleListener`, types; `MockBleApi`
- [ ] A3 `BleHost` + `BleWorker` (thread, queues, stop/join) + TSan tests with `MockBleApi`
- [ ] A4 `FakeBleApi` + scenario format (`tests/ble_scenarios/`) + its own tests
- [ ] A5 `QtBleApi` (adapter, scan, connect chain, write modes, MTU, pairing, error mapping)
- [ ] A6 `BleScanner` (filter, de-dup by id, bounds, batching) + tests
- [ ] A7 `NusLink` (chunking, write modes, timeouts, pairing wait) + GMock tests
- [ ] A8 Error codes in `libs/core` (+ `cs` translations)

Phase B — device model
- [ ] B1 `ConsoleSession` extracted; `DeviceSession` derives; `waitUntilConnected` & co. take `ConsoleSession&`;
  all M1 tests green unchanged
- [ ] B2 `SessionKey` registry, `BleLock` + tests (held, stale, sanitised names)
- [ ] B3 `BleProfile`, CHESTER BLE block, `findByAdvertisement` + tests
- [ ] B4 `BleSession` (open, data path, prompt, reconnect, pairing events, close during connect) + tests (fake + mock)

Phase C — CLI
- [ ] C1 Generalise `ConsoleView`, `runConsole`, command collector, NDJSON formatters to `ConsoleSession`
- [ ] C2 `hio ble scan`, `hio ble console`, `hio ble command`, `hio ble pair`
- [ ] C3 CLI end-to-end tests with fake scenarios (human + `--json`, exit codes, Ctrl+C, lock held by a second
  process)

Phase D — platform
- [ ] D1 macOS Info.plist for GUI and CLI + CTest
- [ ] D2 Legacy-bug regression tests (see below)

Phase E — diagnostics (after 0007 phase A)
- [ ] E1 `bluetooth` check + `hio doctor` output/JSON + tests with fake adapter states

Phase F — hardware and docs
- [ ] F1 HIL tests (`HIO_HIL=1`, CHESTER with Bluetooth shell): scan, connect, command, reconnect after device
  restart, pairing from a fresh bond
- [ ] F2 Manual checks on Linux, Windows, macOS (pairing UI per OS, macOS permission prompt for GUI and CLI)
- [ ] F3 Update CLAUDE.md "Commands", `0006-cli.md` table status, roadmap

## Tests and verification

- **Unit (GMock on `IBleApi`):** chunking at MTU 23/185/247/517 and lines of 1, MTU−3, 4096 bytes; write-mode
  bit test (both bits → without-response); echo only after the last chunk completes; 2 s write timeout →
  Disconnected; `ble.not_paired` only for authentication/encryption errors; pairing wait 60 s and `pairingPrompt`
  after 2 s; reconnect only while seen; explicit reconnect; close during each connect step; state events
  de-duplicated.
- **Line handling:** prompt split across notifications; prompt with ANSI codes; prompt never emitted as a line;
  UTF-8 split across notifications; `#` and level routing on BLE data.
- **Scanner:** de-dup by id with equal names (legacy bug); 256-device bound; RSSI sort; `--all`; discovery error
  without any controller (legacy null-deref).
- **Host (TSan):** concurrent post/stop; stop with 8 links; queue full → `internal.queue_full`; event overflow →
  `LinesDropped`; QtBluetooth objects destroyed on the BLE thread (checked with `QThread::currentThread` asserts
  in the fake).
- **Registry / locks:** J-Link + BLE sessions share 8 slots; BLE lock held by a second process → owner info; stale
  lock taken over.
- **CLI end-to-end (fake scenarios):** `ble scan --json` (0, 2, 300 devices; adapter off → exit 4; permission
  denied → exit 4); `ble command version --json`; `ble command` ends at the prompt before the timeout; `--name`
  ambiguous → exit 2; not paired → exit 6 `ble.not_paired`; second process on the same device → exit 5;
  `ble console --json` with piped stdin and a link drop + `--auto-reconnect`; `ble pair` on a "macOS" scenario →
  exit 2; Ctrl+C → 130 without hang.
- **M1 regression:** the whole M1 suite passes after the `ConsoleSession` extraction.
- **Sanitizers / static analysis / translations:** ASan+UBSan, TSan, clang-tidy, clang-format, `.ts` freshness.
- **HIL (CHESTER with Bluetooth shell):** scan; console with an existing bond; remove bond on both sides → pairing
  with passkey; `ble command version`; device restart → reconnect with `--auto-reconnect`; out of range →
  `ble.disconnected`; one J-Link and one BLE session to the same CHESTER at once.
- **Manual (by the user, before Done):** Linux (GNOME agent and headless `bluetoothctl`), Windows 10/11, macOS 13+
  (permission prompt for the GUI bundle and for `hio` in Terminal; denied → `ble.permission_denied`).

## Risks and open points

- **Pairing UX differs per OS** (BlueZ agent, Windows pairing dialog vs. Settings, CoreBluetooth automatic). Qt 6
  exposes no passkey callbacks; HIL decides whether `requestPairing` on Windows shows the passkey dialog. If it does
  not, `ble pair` on Windows falls back to opening the instructions (needs a plan update and user approval).
- **Error detail for "not paired"** is platform-specific and partly hidden by Qt; the mapping is verified in HIL on
  all three OSes and kept in one function.
- **Firmware facts** (advertised name, NUS in the advertisement, prompt, passkey default) come from the old app and
  py-hardwario only; confirm with the CHESTER firmware team before Done (open question 23).
- **Scanning while connected** may reduce throughput on some adapters; reconnect scans run only while disconnected.
- **Flatpak** needs D-Bus access to `org.bluez` (open question 7, M7).
- **macOS CLI permission** is attributed to the terminal app; behaviour under hardened runtime is checked in M7.

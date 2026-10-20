# 0011 — M5 BLE backend and CLI: `libs/hw/ble`, `ConsoleSession`, `BleSession`, `hio ble …`

- **Status:** In progress (implementation started 2026-10-19 on `v2/m5-ble`; approved 2026-10-19 by Matej Strnad,
  in the revised text with ADR-0127/0128, the minor defaults and `ble command` with the M1 timeout semantics)
- **Milestone:** M5 (plan 1 of 2, ADR-0078; followed by [0012](0012-m5-ble-gui-pairing.md))
- **Author:** Matej Strnad (with Claude), 2026-10-10; revised 2026-10-19 to match the M1–M4 code on `v2/m4-wizards`
- **Related ADRs:** ADR-0002, ADR-0004, ADR-0007, ADR-0009, ADR-0012, ADR-0020, ADR-0022, ADR-0024, ADR-0026,
  ADR-0027, ADR-0031, ADR-0032, ADR-0033, ADR-0053, ADR-0058, ADR-0063, ADR-0068, ADR-0070, ADR-0077,
  ADR-0078–ADR-0089, ADR-0127–ADR-0130, ADR-0141, ADR-0142
- **Depends on:** 0002 (M1: `DeviceSession`, `ConsoleLog`, `LineAssembler`, CLI console) and
  [0007](0007-m4-wizard-framework-diagnostics.md) phase A (`libs/diag`), both implemented on `v2/m4-wizards`.
  Implemented on branch `v2/m5-ble`. [0012](0012-m5-ble-gui-pairing.md) follows after 0008/0009.

> **Revision 2026-10-19 (before approval):** the text was synchronised with the code as it is after M1–M4
> (`DeviceSession`, `SessionRegistry`, `ConsoleFileSink`, `LineAssembler`, CLI console and `ble command` semantics,
> error-text rules, `HIO_TEST_OVERRIDES`). Two decisions changed: the fake BLE backend uses `HIO_TEST_OVERRIDES`
> (ADR-0127), and BLE line assembly + console file run on the BLE thread (ADR-0128).

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
| `hio ble scan [--timeout S=5] [--all] [--profile ID=chester]` | One scan of `S` seconds (1–60). Rows matching the profile by name (ADR-0083, ADR-0142: name pattern only; `nusAdvertised` is information) or all with `--all` (devices without a profile have `"profile": null`), sorted by RSSI. Human: table as above. `--json`: `data = {"adapter":{"name","address"}, "devices":[{"id","name","rssi","nusAdvertised","profile":"chester"\|null,"inUseBy":{"app","pid"}\|null}]}`. No device → empty list, exit 0. Adapter or permission problem → error, exit 4. |
| `hio ble console (-a/--address ID \| --name N) [--profile ID] [--auto-reconnect] [--history-file P] [--console-file P]` | Interactive console (M1 UI). `--name N`: 5 s scan, case-insensitive substring of the advertised name among profile matches; exactly one match required (else `ble.device_not_found` or `usage.invalid_argument` listing the ids). `--json`: the M1 NDJSON stream (`line`, `connection`, `dropped`, `warning`) plus `{"type":"connection",…,"transport":"ble","mtu":247}`. |
| `hio ble command CMD (-a ID \| --name N) [-t/--timeout S=3.0] [--console-file P]` | Connect, send each line of `CMD`, collect shell output until the prompt is seen after the last line, or the timeout — with the **same semantics as the M1 J-Link `command`** (timeout counted from the last echo, `"truncated"` in `--json`), sharing its collector (`ResponseCollector` / `LineQueue` moved out of `chester_commands.cpp`). The prompt only ends collection early; without it the timeout ends it as for J-Link. Disconnect. Human: output lines; `--json`: `data = {"command","output":[…],"log":[…],"truncated"}` (M1 shape). Connect failure → that error's exit code. |
| `hio ble pair (-a ID \| --name N)` | App-initiated pairing (ADR-0084). Human: "Paired." / "Already paired."; `--json`: `data = {"id","paired":true}`. macOS → `ble.pairing_not_supported` (exit 2). |

Exit codes per ADR-0024 (no new categories): Bluetooth adapter / stack / permission → 4 (dependency); BLE device
problems → 6 (target); `ble.in_use_by_other_process` → 5 (probe, same meaning "connection busy" as J-Link);
`ble.pairing_not_supported` → 2 (usage). 0006-cli's exit-code meanings are extended in the text ("… or Bluetooth
adapter", "… or BLE device").

## Design

### Modules

```
libs/hw/ble/      hio_hw_ble (QtCore, Qt6::Bluetooth)  include/hio/hw/ble/*.h  src/*  src/fake/* (HIO_TEST_OVERRIDES, ADR-0127)  tests/*
libs/device/      + console_session.h, ble_session.{h,cpp}, ble_lock.{h,cpp}, session_key.h;
                    profile.h (+BleProfile), profiles/chester.cpp (+BLE block);
                    changed: device_session.h, session_registry.h, session_events.h, console_file_sink.h,
                    line_assembler.h
libs/diag/        + bluetooth check (environment.cpp), EnvironmentContext::ble
libs/core/        + error codes, lcBle ("hio.ble")
app/cli/src/      ble_commands.{h,cpp}; command_collector.{h,cpp} (ResponseCollector + LineQueue moved out of
                    chester_commands.cpp); console_ui / console_events / chester_commands take ConsoleSession&
app/cli/macos/Info.plist.in, app/gui/macos/Info.plist.in (new; the GUI has no template yet)
tests/ble_scenarios/*.json   fake scenarios shared by unit, CLI e2e and integration tests
```

Top-level `find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Bluetooth LinguistTools)`. `hio_hw_ble` is created with
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
[[nodiscard]] std::unique_ptr<IBleApi> createBleApi();   // QtBleApi, or FakeBleApi when built with
                                                          // HIO_TEST_OVERRIDES and HIO_FAKE_BLE_SCENARIO is
                                                          // set (ADR-0087, ADR-0127)
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
  (256, Reject → `internal.queue_full`); events out through a `BoundedQueue<BleEvent>` (10 000,
  `DropOldestMatching`: only data lines / advertisements are dropped and counted, never connection, pairing or
  write-result events — as `DeviceSession` does) per subscriber (scanner, each session), each with a coalesced
  queued wake-up. `stop()` disconnects all
  links, deletes the QtBluetooth objects on the BLE thread, quits, joins (3 s bound).
- **`BleScanner`** (façade, owner thread) — `start(ScanOptions{duration, profileFilter, all})`, `stop()`, signal
  `devicesChanged(QList<BleDeviceInfo>)` batched ≤ 4/s; `BleDeviceInfo { BleDeviceId id; std::string name; int rssi;
  bool nusAdvertised; const DeviceProfile* profile; steady_clock::time_point lastSeen; }`; de-duplicated by `id`,
  max **256** devices (oldest by `lastSeen` evicted), names truncated at 64 bytes. Used by the CLI and by 0012.
- **`NusLink`** (BLE thread, platform-neutral, on `IBleApi`) — ADR-0082: line → chunks of `mtu − 3`; with-response:
  one chunk in flight; without-response: ≤ 16 queued; per-line completion token; 2 s write timeout →
  `ble.write_timeout` + link down; pairing wait (CCCD pending) up to 60 s with a `PairingPrompt` event after 2 s.
  Per link it also owns (ADR-0128) the `LineAssembler` (console target's rules), prompt detection (ADR-0082, on the
  assembler's partial line) and the `ConsoleFileSink` (flushed by a ≤ 250 ms BLE-thread timer and on close); the
  owner receives finished `ConsoleLine`s.

### `libs/device` (`hio::device`)

**`ConsoleSession`** (ADR-0079) — extracted from what `DeviceSession` already offers, signatures unchanged:

```cpp
enum class Transport : std::uint8_t { JLink, Ble };
struct SessionIdentity { Transport transport; std::string id /*SN as decimal or BleDeviceId*/; std::string displayName; };
enum class Capability : std::uint8_t { Reset = 1, LogChannel = 2, Operations = 4 };
Q_DECLARE_FLAGS(Capabilities, Capability)

class ConsoleSession : public QObject {
    Q_OBJECT
public:
    [[nodiscard]] virtual Status sendLine(std::string line) = 0;     // queued; error only if queue full/closed
    [[nodiscard]] virtual Status reconnect() = 0;
    void close();                     // non-virtual, idempotent: calls closeSession() once
    [[nodiscard]] virtual const ConsoleLog& log() const = 0;          // owner thread only (ADR-0063)
    [[nodiscard]] virtual const ConnectionEvent& connection() const = 0;
    [[nodiscard]] virtual SessionIdentity identity() const = 0;
    [[nodiscard]] virtual Capabilities capabilities() const = 0;
    [[nodiscard]] virtual const DeviceProfile& profile() const = 0;
    [[nodiscard]] virtual std::string_view consoleTargetId() const = 0;
    [[nodiscard]] virtual std::filesystem::path consoleFile() const = 0;   // empty = none (plan 0005 rules)
signals:
    void linesReceived(const QList<hio::device::ConsoleLine>&);
    void connectionChanged(const hio::device::ConnectionEvent&);
    void linesDropped(quint64 count);
    void warning(const hio::Error&);
    void promptSeen();                                                // BLE only (ADR-0082); never for J-Link
protected:
    virtual void closeSession() = 0;  // the concrete close; each final class's destructor calls its own
                                      // close path directly (no virtual call from a destructor)
};
[[nodiscard]] Status waitUntilConnected(ConsoleSession&, std::chrono::milliseconds, const std::stop_token& = {});
[[nodiscard]] Status waitForFirstAttempt(ConsoleSession&, std::chrono::milliseconds, const std::stop_token& = {});
```

`DeviceSession` derives from it (identity = `{JLink, "<sn>", "J-Link <sn>"}`, capabilities = all; `consoleTargetId()`
= its target id). Everything else of `DeviceSession` (`probe()`, `target()`, `suspend`/`resume`, operations,
`diagnose`, `startConnection`, operation signals, `waitForOperation`) stays on `DeviceSession`, unchanged.

**`ConnectionEvent`** gains `Transport transport = Transport::JLink` and `std::optional<std::uint16_t> mtu` (BLE,
set on Connected); `targetMillivolts` stays J-Link only. `ConnectionState::ProbeOpen` is never used by BLE.

**`LineAssembler`** gains `[[nodiscard]] std::string_view partial() const noexcept` (read-only view of the
incomplete line) for prompt detection; behaviour otherwise unchanged.

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

- Data path (ADR-0128): NUS bytes → `LineAssembler` (target `consoleTargetId`'s rules) + prompt detection
  (ADR-0082) + `ConsoleFileSink` (file name `<profile>-ble-YYYYMMDD-HHMMSS-<sanitised id>.log`, M1
  rotation/retention, connection lines ` @ `) on the **BLE thread** → finished `ConsoleLine`s through the session's
  bounded event queue → `ConsoleLog` + batched signals on the owner thread.
- Reconnect (ADR-0082): with `autoReconnect`, after `Disconnected` the session asks the host for a background scan
  and retries every 3 s while the device was seen in the last 15 s; `reconnect()` always retries at once.
  `close()` during connecting cancels within one BLE-thread turn.
- **`BleLock`** — as `ProbeLock` (ADR-0058, ADR-0083), `locks/ble-<sanitised id>.lock` (`[A-Za-z0-9-]` kept,
  everything else → `-`).
- **`SessionRegistry`** — `SessionKey = std::variant<ProbeKey, BleKey>` (`ProbeKey{std::uint32_t sn}`,
  `BleKey{BleDeviceId id}`); stores `ConsoleSession*`; `checkAvailable(std::optional<SessionKey>)`;
  `sessionFor(const SessionKey&)`; max 8 sessions total. The existing `checkAvailable(std::optional<std::uint32_t>)`
  and `sessionForProbe(std::uint32_t)` stay as thin wrappers so M1–M4 call sites don't change. `bleInUse(id)` →
  this-process session or other-process lock owner (for scan rows).
- **`ConsoleFileSink`** (ADR-0128) — `open()` takes a `SessionKey` instead of the probe serial number; J-Link file
  names unchanged; retention parses the kind from the file name and asks `ProbeLock::owner` or `BleLock::owner`
  (R45/R48 live-file rules for both). Documented owner: the session worker (J-Link) or the BLE thread (BLE).
- **Profiles** — `BleProfile` and `DeviceProfile::ble` per ADR-0089; CHESTER: name `CHESTER`, NUS UUIDs, prompt
  `bt_nus:~$ `, console target `app`, passkey hint, pairing guide steps (slots `ble-enable`, `ble-pair-os`; 0012
  draws them). `ProfileRegistry::findByAdvertisement`.

### `libs/diag`

`bluetooth` check per ADR-0088 (after `app-dirs`; 5 s; Warning only; the 16-check limit still holds).
`EnvironmentContext` gains `std::shared_ptr<ble::BleHost> ble` (nullptr → the check creates a short-lived host).
Checks run on a plain thread with no Qt objects crossing threads, so the check posts an `AdapterQuery` command to
the host and waits on its reply (bounded, 5 s) — it never touches QtBluetooth itself. It reads
`IBleApi::adapter()` only; it never scans (no macOS prompt).

### macOS (ADR-0086)

`app/gui/macos/Info.plist.in` (new; `MACOSX_BUNDLE_INFO_PLIST`; the keys CMake's default template produces today
from the bundle properties in `app/gui/CMakeLists.txt` + `NSBluetoothAlwaysUsageDescription`);
`app/cli/macos/Info.plist.in` linked into `hio` with `-Wl,-sectcreate,__TEXT,__info_plist,<file>`. CTest on macOS
verifies both.

### Error codes added (texts translatable, ADR-0027)

Default texts contain no placeholders (rule in `libs/core/src/error.cpp`); `%1`/`%2`/`%3` below mark where the call
site builds the message with the device name / program / PID via `fail(code, message, …)`, and the table default is
the text without them (e.g. "The Bluetooth device was not found."). `WizardId::BlePairing` already exists.

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
| `ble.no_pairing_agent` (HIL H1, 2026-10-20) | Dependency | "No program is ready to ask for the Bluetooth passkey." / no pairing agent is running (on GNOME the agent may only be active in Settings → Bluetooth) / "Pair the device from the system Bluetooth settings, or with bluetoothctl: `agent KeyboardOnly`, `default-agent`, `pair <id>`." Linux only: a pairing request failing with Qt `PairingError` within 2 s (`detail::pairingFailure`, `qt_ble_api.cpp`); HIL must confirm the timing with an agent and a quick cancel, and other desktops (KDE, headless). | BlePairing |

### Threading summary

Owner thread (GUI / CLI main): `BleScanner`, `BleSession` façades, `ConsoleLog`, `CommandHistory`, registry,
locks. BLE thread (one per application, ADR-0080): `BleWorker`, `IBleApi` implementation, all QtBluetooth objects,
`NusLink`s with their `LineAssembler`, prompt detection and `ConsoleFileSink` (ADR-0128). Only the bounded queues
cross threads.

### Bounds

BLE command queue 256 · event queue 10 000 per subscriber (DropOldest + `LinesDropped`) · scan list 256 devices,
name 64 B · outgoing line 4 KiB · chunks in flight 1 (with-response) / 16 (without) · partial line 4 KiB ·
ConsoleLog 100 000 lines/channel · console files and history as M1 · sessions 8 total · timeouts: scan default 5 s
(1–60), connect 30 s (HIL H3; 60 s while pairing; a failed attempt retried after 1 s within it, H6), controller close wait 3 s (H2), write 2 s, reconnect 3 s while seen in 15 s, `ble command` 3 s,
`BleHost::stop` 3 s, doctor check 5 s, scanner signal ≤ 4/s.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| M5 split | two / three / one | two: backend + CLI (0011), GUI + wizard (0012) | ADR-0078 |
| Session API | common `ConsoleSession` / transport inside DeviceSession / separate | common `ConsoleSession` interface | ADR-0079 |
| BLE threading | one shared BLE QThread / per session / owner thread | one shared BLE QThread | ADR-0080 |
| NUS content | shell only / shell + logs / both | follow the old app: shell only, routing rules kept | ADR-0082 |
| Scan filter | profile name + NUS UUID / name only / UUID only | profile name pattern or NUS UUID, `--all`; amended 2026-10-19: name pattern only | ADR-0083, ADR-0142 |
| Identity, lock, restore | address/UUID + lock + restore / no lock / no restore | address/UUID + `BleLock` + tab restore | ADR-0083 |
| Pairing | OS + wizard / + app-initiated / error only | OS + app-initiated (Linux/Windows) + wizard | ADR-0084 |
| Passkey hint | default 123456 via PIB / label / PIB only | default 123456, changeable via PIB | ADR-0084 |
| Reconnect | as J-Link option / always / never | same option as J-Link | ADR-0082 |
| CLI commands | scan+console / +command / +command+pair | scan, console, command, pair | ADR-0085 |
| macOS CLI permission | embedded plist / unsupported / CLI in bundle | embedded Info.plist | ADR-0086 |
| Test seam | `IBleApi` + fake (test builds) / always compiled / GMock only | `IBleApi` + fake, test builds only | ADR-0087 |
| Fake build switch (2026-10-19) | reuse `HIO_TEST_OVERRIDES` / new `HIO_TEST_FAKES` | reuse `HIO_TEST_OVERRIDES` | ADR-0127 |
| BLE line assembly + console file (2026-10-19) | BLE thread / owner thread + flush timer | BLE thread; sink keyed by `SessionKey` | ADR-0128 |
| Bluetooth checks | doctor / setup step / pairing wizard only | doctor + setup step | ADR-0088 |
| Profile shape | `DeviceProfile::ble` / BLE target / separate registry | `DeviceProfile::ble` | ADR-0089 |
| Minor defaults: all bounds and timeouts above; error codes and categories (adapter → dependency, device → target, busy → probe); `lcBle` = `hio.ble`; console file name `<profile>-ble-…`; `--name` substring match; supported stacks = BlueZ of Ubuntu 22.04 (5.64) or newer, Windows 10 22H2+, macOS 13+ (ADR-0012) | accept / change | accepted (2026-10-19) | — |

## Decisions during implementation (2026-10-19, by Matej Strnad)

Answers to the implementation agent's questions; they override the text above where it differs.

| # | Question | Decision | ADR |
|---|---|---|---|
| Q1 | Module layout (`hw/ble` cannot use `libs/device` types) | `hw/ble` transport only; `ILinkHandler` per link implemented in `libs/device` on the BLE thread; `BleScanner` with injected profile matcher, `BleDeviceInfo::profileId` | ADR-0129 |
| Q2 | `BleHost::stop()` 3 s bound | warn after 3 s, then keep waiting | ADR-0130 |
| Q3 | BLE device already used in this process | `ble.in_use_by_other_process` (exit 5), message "The device … is already used by this application." | — |
| Q4 | `hio ble pair` | pairs through `BleHost` without connecting, takes the `BleLock` (busy → exit 5); `IBleApi::isPaired(id)` added for "Already paired." | — |
| Q5 | NDJSON connection events | `"transport"` on every connection event (J-Link `"jlink"`, additive); `"mtu"` on BLE events only | — |
| Q6 | Connect deadlines | 20 s → `ble.connect_failed`; CCCD pending 60 s (no passkey) → `ble.not_paired`; CLI safety timeout 65 s (amended by H3, 2026-10-20: connect 30 s; CLI safety timeouts `ble console`/`command`/`pair` 95 s) | — |
| Q7 | Linux "stack unavailable" | `/sys/class/bluetooth/hci*` exists but Qt sees no adapter → `ble.stack_unavailable`, else `ble.adapter_not_found`; no D-Bus | — |
| Q8 | Texts | passkey hint = "The passkey is 123456 unless it was changed when the device was configured."; pairing guide steps `bluetooth`/`ble-enable` ("Make sure the device is powered and its Bluetooth is on.") and `pair`/`ble-pair-os` ("When the system asks for the passkey, enter it in the system dialog."); causes: adapter_off "Bluetooth is turned off in the system settings or with a hardware switch.", stack_unavailable "The Bluetooth service of the system (BlueZ on Linux) is not running.", permission_denied "Bluetooth access was denied in the privacy settings.", pairing_not_supported "macOS pairs a device automatically when it connects."; CLI prompt "Pairing: enter the passkey in the system dialog." + profile passkey hint; "-- connected (Bluetooth, MTU 247) --"; disconnect line shows the error message (M1 rule) | — |
| Q9 | First-connect failure | as `DeviceSession` (Stopped without `--auto-reconnect`, Disconnected + retry with it); always fatal: `ble.adapter_*`, `ble.permission_denied`, `ble.stack_unavailable`, `ble.service_not_found`, `ble.not_paired`, `ble.pairing_failed`, `ble.in_use_by_other_process` | — |
| — | BLE thread type (after A–E, TSan) | owned `std::jthread` running a `QEventLoop` instead of `QThread` | ADR-0141 |
| — | NUS-only devices listed as CHESTER on a real adapter | profile match by name pattern only; NUS-only devices only with `--all` | ADR-0142 |
| — | `BleSession::close()` blocked up to 1 s for the console file | non-blocking: `close()` returns at once, the file is closed on the BLE thread | — |
| — | Agent's own choices accepted | `BleLock` mirrors `ProbeLock` (shared string-keyed helper, `ProbeLockOwner` reused); retention passes the sanitised id to `BleLock::owner`; `SessionRegistry::bleInUse` returns `{session, otherProcess}`; BLE file header "Console: CHESTER, Bluetooth <id> (<name>), hio <version>"; fake scenario field names modelled on `tests/fake_jlink`; line > 4 KiB → `usage.invalid_argument`; `ble command` write budget 2 s per line + 500 ms; pairing prompt only logged in `--json`; doctor check adds no report facts; CI installs `qtconnectivity` | — |

## Open findings (independent review round 1, 2026-10-19)

| # | Sev. | Finding | Where |
|---|---|---|---|
| B1 | high | `#include <bits/chrono.h>` (libstdc++ internal) breaks libc++ / MSVC builds; also the pre-existing one in `check_runner_test.cpp` | `ble_commands.cpp:36` + 6 tests |
| B2 | high | Background-scan restart spins (owner ↔ BLE thread, D-Bus call per cycle) when the adapter is unusable during auto-reconnect; the error is never shown | `ble_session.cpp:533-536, 582, 673`; `ble_worker.cpp:147` |
| B3 | med | `close()` whose CloseLink cannot be posted (queue full) leaves the link connected and the file open until `BleHost::stop()` while lock and slot are released | `ble_session.cpp:455-462` |
| B4 | med | No synchronous back-pressure: NusLink rejects lines beyond 256 asynchronously; console paste and `ble command` lose lines (M1 review R39 regression for BLE) | `nus_link.cpp:199`, `ble_session.cpp:481`, `command_collector.cpp:112` |
| B5 | med | A controller error emitted synchronously inside `connectToDevice()` is dropped → 20 s wait and `ble.connect_failed` instead of the real code (plausible, Qt source) | `qt_ble_api.cpp:361-370, 424`; `nus_link.cpp:97-108` |
| B6 | med | `-a` ids not canonicalised: lower-case address → `ble pair` times out, different lock file / registry key / file name for the same device | `ble_commands.cpp:181`, `qt_ble_api.cpp:202`, `ble_worker.cpp:354`, `ble_lock.cpp` |
| B7 | med | `ble.not_paired` mapping contradicts ADR-0084: any CCCD write error while pairing pending → not_paired (fatal); RX write auth/encryption errors → write_failed | `qt_ble_api.cpp:98-104` |
| B8 | med (plausible) | macOS: Undetermined permission is never requested (`QCoreApplication::requestPermission`); Qt 6.5+ discovery may fail without a prompt → always `ble.permission_denied` | `qt_ble_api.cpp:236-251, 303-333` |
| B9 | low | Dangling `HostShared::worker` if an exception leaves `loop.exec()` | `ble_host.cpp:62-83` |
| B10 | low | `NusLink::disconnect()` (explicit reconnect) drops the line in flight without echo or warning | `nus_link.cpp:69-95` |
| B11 | low | Lock released before the BLE-thread disconnect (accepted with the non-blocking close; related to B3) | `ble_session.cpp:462` |
| B12 | low | Test gaps for B2–B6, background scan error, CloseLink on full queue; timing-sensitive tests (`ble_session_test.cpp:563` 1.5 s sleep, `nus_link_test.cpp:236-245`) | tests |

User decision 2026-10-19: fix all B1–B12. B3 → closes use reserved capacity (a separate close path bounded by the
8-link limit, never dropped; `close()` stays non-blocking). B7 → follow ADR-0084: `ble.not_paired` only with a
platform authentication/encryption detail (CCCD or RX write), otherwise `ble.connect_failed` / `ble.write_failed`;
the per-OS mapping is confirmed in HIL. B8 → GUI and CLI call `QCoreApplication::requestPermission` on the main
thread before the first scan or connect when the state is undetermined (confirmed in the macOS manual check, F2).
B1 also adds a CI check rejecting `#include <bits/…>`.

## Open findings (focused review round 2 of the B1–B12 fixes, 2026-10-19)

B1, B2, B4–B7, B9, B10 verified fixed; B3 introduced N1/N2; B12 OK apart from N1.

| # | Sev. | Finding | Where |
|---|---|---|---|
| N1 | med | Regression from B3: the worker drains all commands before closes, so a reopen of the same device can connect and open its file before the old link/file is closed (breaks the guarantee at `ble_session.cpp:454-457`; `ReopenRightAfterCloseIsSafe` passes by timing) | `ble_worker.cpp:77-89` |
| N2 | low | A CloseLink without a reservation (failed open) is still pushed into the 8-slot close queue and can displace a reserved close → B3 symptom | `ble_host.cpp:110-116`, `ble_session.cpp:405-406` |
| N3 | low | Stopped from a fatal scan error is not reported to the BLE-thread handler: no "stopped" line in the console file, and rejected lines still say "Wait for the console to reconnect." | `ble_session.cpp:243-247, 583-589` |
| N4 | low (plausible) | A scan-fatal Stopped can flip back to Disconnected when an in-flight automatic connect ends with another non-fatal error | `ble_session.cpp:282` |
| N5 | low (plausible) | B5 window: a write between `lost()` and the deferred report returns `ble.write_failed` and can hide the real code (e.g. `ble.not_paired`) | `qt_ble_api.cpp:373-387, 556` |
| N6 | low | B8: `readyAdapter` returns the pre-request `AdapterInfo` after a granted permission; an adapter that is off is reported only later | `ble_commands.cpp:199-212` |

User decision 2026-10-20: fix all N1–N6 before HIL (verify, no further full review). Also remove the pre-existing
J-Link model names (plans 0003/0004/0007, `plan/gui/mockups/index.html`, `tst_layout.qml`,
`connection_view_model_test.cpp`) on this branch; use "J-Link probe" / neutral fixture strings.

## HIL findings (Linux, BlueZ 5.85, GNOME, one CHESTER with Bluetooth shell, 2026-10-20)

Passed: `ble scan` (15 CHESTERs, name-only filter), `doctor` bluetooth row, `ble pair` "Already paired." (also with a
lower-case address), `ble command` (human + `--json`), J-Link RTT + BLE to the same CHESTER at once. The CHESTER
advertises a vendor UUID, not NUS; NUS is present after connecting (confirms ADR-0142).

| # | Sev. | Finding |
|---|---|---|
| H1 | med | Without an active desktop pairing agent (GNOME, Settings closed; also with Settings → Bluetooth open) `ble pair` and a direct BlueZ `Device1.Pair` fail at once with no dialog (BlueZ "Authentication Canceled", Qt `PairingError`); the CLI reports `ble.pairing_failed` "wrong passkey / stale bond". Pairing in `bluetoothctl` with its own agent worked. Needs: a distinct message/suggestion (no agent → pair with `bluetoothctl` or the system Bluetooth settings), possibly a CLI-registered BlueZ agent (decision). |
| H2 | high | On close the `QLowEnergyController` is deleted in `ClosingState` ("not Unconnected when deleted") on every run; afterwards connects failed repeatedly within 0.12 s with `QLowEnergyService error 3` (descriptor write) → `ble.connect_failed` "weak signal", while BlueZ still showed the device connected. The BLE thread must wait (bounded) for `disconnected` before deleting the controller. |
| H3 | med | BlueZ connect + service resolution takes 6–20 s here (busy office); the 20 s connect deadline fails `ble console` and some `ble command` runs ("not connected within 20000 ms"). Deadline/retry needs a decision. |
| H4 | low | MTU reported as 23 on BlueZ (Qt's D-Bus backend), so lines go in 20-byte chunks; check whether the real ATT MTU is available. |
| H5 | low | `ble command --json` output starts with an empty string (`"output": ["", "Uptime: …"]`). |

User decisions 2026-10-20: fix H1–H5. H1 → detect the immediate "Authentication Canceled" / `PairingError` without a
dialog on Linux and report a distinct "no pairing agent" error (new code `ble.no_pairing_agent`, Dependency) with
the suggestion to pair from the system Bluetooth settings or with `bluetoothctl` (`agent KeyboardOnly`,
`default-agent`, `pair <id>`); no own BlueZ agent, no Qt6::DBus. H3 → connect deadline 30 s (pairing wait stays
60 s; CLI safety timeouts adjusted). H2 → the BLE thread waits (bounded) for `disconnected` before deleting the
controller. H4 → use the real ATT MTU if BlueZ exposes it, otherwise document 23. H5 → no empty first output line.

HIL re-run 2026-10-20 after the H1–H5 fixes: H2 confirmed fixed (8/8 back-to-back `ble command` runs OK, no
"not Unconnected when deleted"); connects take 2–25 s, so H3's 30 s is needed. New / reopened:

| # | Sev. | Finding |
|---|---|---|
| H5 | low | **Reopened:** the real CHESTER answers each command with an empty line first (13 ms after the send, no prompt before it); the fix only drops an empty line after a detected prompt. `ble command --json` still gives `["", "Uptime: …"]`. |
| H6 | med | About 1 in 4 BlueZ connects fail with `QLowEnergyController::UnknownError` (error 1): after ~25 s (BlueZ's own connect timeout), or at once when started right after a failed attempt; reported as `ble.connect_failed` and the session stops (no `--auto-reconnect`). |
| H7 | low | With `-a`, the console-file header repeats the address as the name: "Bluetooth D2:…:C9 (D2:…:C9)". |

User decisions 2026-10-20: H6 → a controller `UnknownError` / connection error during connect is retried after 1 s
while the 30 s connect deadline has not passed; only the final failure is reported. H5 → drop one empty line that is
the first response after a sent line (the fake CHESTER behaves like the real one). H7 → the header shows the name
only when it is known.

HIL re-run 2026-10-20 after the H5–H7 fixes: H5 and H7 confirmed fixed. H6: with BlueZ discovering in the background
(GNOME Settings → Bluetooth and `bluetoothctl` open) 2 of 6 and 1 of 2 runs failed; the log shows Qt's D-Bus
`Device1.Connect` giving up after 25 s (`org.freedesktop.DBus.Error.NoReply`) while BlueZ keeps the attempt, so every
retry gets `org.bluez.Error.InProgress` at once. With discovery off: **20/20 OK, no retries, connect 2–21 s.**

| # | Sev. | Finding |
|---|---|---|
| H8 | low | Retrying every 1 s on `org.bluez.Error.InProgress` is useless: BlueZ is still working on the earlier attempt. Better: after a `NoReply`/`InProgress`, wait for BlueZ instead of starting new attempts (Qt reports both only as `UnknownError`; detail text needed). |
| H9 | note for 0012 | Continuous discovery slows LE connects a lot in busy places; the GUI picker (ADR-0081, continuous scan) must stop scanning while a BLE tab connects. |

User decision 2026-10-20: fix H8 (after `NoReply` / `InProgress`, wait for BlueZ's pending attempt instead of
starting new ones, within the 30 s deadline).

Not yet run: console interactive keys (tty), reconnect after device restart, out of range, pairing from a fresh bond via the app.

## Implementation steps

Phase A — BLE library
- [x] A0 Prerequisite check: M1 (0002) and 0007 phase A implemented (otherwise stop and report)
- [x] A1 CMake: `Qt6::Bluetooth`, `hio_hw_ble` target, fake sources only under `HIO_TEST_OVERRIDES` (ADR-0127); `lcBle`
- [x] A2 `IBleApi`, `IBleListener`, types; `MockBleApi`
- [x] A3 `BleHost` + `BleWorker` (thread, queues, stop/join) + TSan tests with `MockBleApi`
- [x] A4 `FakeBleApi` + scenario format (`tests/ble_scenarios/`) + its own tests
- [x] A5 `QtBleApi` (adapter, scan, connect chain, write modes, MTU, pairing, error mapping) — compiled and
  unit-tested only in its mapping functions; behaviour is covered by F1/F2 (ADR-0087)
- [x] A6 `BleScanner` (filter, de-dup by id, bounds, batching) + tests
- [x] A7 `NusLink` (chunking, write modes, timeouts, pairing wait) + GMock tests
- [x] A8 Error codes in `libs/core` (+ `cs` translations)

Phase B — device model
- [x] B1 `ConsoleSession` extracted; `DeviceSession` derives; `waitUntilConnected` / `waitForFirstAttempt` take
  `ConsoleSession&`; `ConnectionEvent` transport + mtu; `LineAssembler::partial()`; all M1–M4 tests green unchanged
- [x] B1b `ConsoleFileSink` keyed by `SessionKey` (ADR-0128) + retention with `BleLock` + tests (J-Link file names
  unchanged)
- [x] B2 `SessionKey` registry, `BleLock` + tests (held, stale, sanitised names)
- [x] B3 `BleProfile`, CHESTER BLE block, `findByAdvertisement` + tests
- [x] B4 `BleSession` (open, data path, prompt, reconnect, pairing events, close during connect) + tests (fake + mock)

Phase C — CLI
- [x] C1 Generalise `ConsoleView`, `runConsole`, `waitForConsoleConnection`, `connectionEventJson` (+ transport,
  mtu) to `ConsoleSession`; move `ResponseCollector` / `LineQueue` to `command_collector.*`; `endsTheCommand` gains
  the `ble.*` link-loss codes
- [x] C2 `hio ble scan`, `hio ble console`, `hio ble command`, `hio ble pair`
- [x] C3 CLI end-to-end tests with fake scenarios (human + `--json`, exit codes, Ctrl+C, lock held by a second
  process)

Phase D — platform
- [x] D1 macOS Info.plist for GUI and CLI + CTest (written; the CTests run only on macOS — first run in the
  macOS CI job / F2)
- [x] D2 Legacy-bug regression tests (see below)

Phase E — diagnostics (after 0007 phase A)
- [x] E1 `bluetooth` check + `hio doctor` output/JSON + tests with fake adapter states

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
- **MTU on Linux (HIL H4, 2026-10-20):** Qt's BlueZ D-Bus backend reports the ATT MTU as 23 (the negotiated MTU is
  only the D-Bus property `GattCharacteristic1.MTU`, which Qt does not read; no own D-Bus, ADR-0004/0080), so lines
  go in 20-byte chunks on Linux - correct, only slower. macOS/Windows values are checked in F2.
- **Scanning while connected** may reduce throughput on some adapters; reconnect scans run only while disconnected.
- **Flatpak** needs D-Bus access to `org.bluez` (open question 7, M7).
- **macOS CLI permission** is attributed to the terminal app; behaviour under hardened runtime is checked in M7.

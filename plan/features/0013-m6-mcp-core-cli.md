# 0013 — M6 MCP core, tools and CLI: `libs/mcp`, low-level target operations, `hio mcp`

- **Status:** Draft
- **Milestone:** M6 (plan 1 of 2, ADR-0094; followed by [0014](0014-m6-mcp-gui.md))
- **Author:** Matej Strnad (with Claude), 2026-10-16
- **Related ADRs:** ADR-0004, ADR-0007, ADR-0008, ADR-0019, ADR-0020, ADR-0022, ADR-0024, ADR-0027, ADR-0028,
  ADR-0031, ADR-0035–ADR-0040, ADR-0053, ADR-0058, ADR-0063, ADR-0068, ADR-0069, ADR-0079, ADR-0082, ADR-0083,
  ADR-0094–ADR-0107
- **Depends on:** 0002 (M1) and 0003 (M2) implemented. The `diagnose`/`doctor` tools need
  [0007](0007-m4-wizard-framework-diagnostics.md) phase A (`libs/diag`, `Diagnose` operation). BLE devices need
  [0011](0011-m5-ble-backend-cli.md) (`ConsoleSession`, `BleSession`). Phases A–C can start right after M2. Phase D
  adds the diagnosis and BLE tools once their plans are implemented, and is skipped (tools not registered) until then.

## Context

AI assistants (Claude Code, Claude Desktop, Cursor, VS Code) should be able to observe and drive HARDWARIO devices:
read the shell and logs, run commands, flash, reset and inspect memory. rttt 1.9.2 already does this for one RTT
session (`rttt/connectors/mcp_server.py`, read-only reference in `../refs/rttt-1.9.2`): FastMCP over Streamable
HTTP, an optional bearer token, ring buffers of 5000 lines, 21 tools and the `debug_device` prompt. Its behaviour is
our reference (ADR-0099), with these differences:

- several devices instead of one, with stable ids (ADR-0098);
- consent and visible logging for destructive tools (ADR-0101, ADR-0105);
- Origin and Host checks and hard limits (ADR-0102);
- `send_command` ends at the prompt (ADR-0104);
- no `/upload` endpoint (ADR-0100);
- the same server is available over stdio and HTTP, from the CLI and the GUI (ADR-0008, ADR-0106).

There is no usable C++ SDK, so the server is our own code on QtNetwork + QJsonDocument (ADR-0096) and speaks both
MCP 2025-11-25 and 2026-07-28 (ADR-0095).

## Goals / Non-goals

**Goals**
1. `libs/mcp` (QtCore + QtNetwork): the JSON-RPC 2.0 core, both protocol dialects, the tool / resource / prompt
   registry, the stdio transport and a bounded HTTP/1.1 transport. Seams `IDeviceProvider` and `IConsentBroker`.
2. All v1 tools (ADR-0099), the `debug_device` prompt, the resources, progress and cancellation (ADR-0107).
3. `libs/device`: new operations `halt`, `go`, `targetStatus`, `writeMemory`, `readMemory` with width,
   `readRegisters`, `memoryZones` and `writeFlash`, plus the JLinkARM declarations they need (ADR-0028) and the
   profile additions `ram`, `peripherals` and `shellPrompt`, with RTT prompt detection (ADR-0104).
4. CLI: `hio mcp [--http]` (ADR-0106) and the CLI commands for the new operations (ADR-0099).
5. Every error is an `hio::Error` (EN + CS for the CLI and GUI; MCP renders English, ADR-0103). Every buffer,
   queue and timeout is bounded and documented.

**Non-goals**
- GUI: tabs as devices, consent dialogs, settings, token UI, markers, activity list, indicator (0014).
- Remote access, TLS, OAuth (ADR-0102), upload endpoint (ADR-0100).
- Tasks extension, sampling, elicitation/MRTR, completion, roots, logging (ADR-0107).
- J-Link probe firmware update, BLE pairing, PIB/UICR write, catalog browsing over MCP (ADR-0099).

## User experience

No GUI changes. Typical use from Claude Code:

```
$ claude mcp add --transport stdio hio -- hio mcp --allow flash
> "Flash build/merged.hex to my CHESTER and check that the LTE modem attaches."
  → list_devices → connect(jlink:<SN>) → flash(path=…) → wait_for_connection
  → send_command("lte state") → wait_for_state("attached: yes", timeout=180)
```

A tool error the AI receives (tool result with `isError: true`):

```json
{"error":{"code":"mcp.tool_not_allowed","category":"usage",
  "message":"The tool erase is not allowed by this server.",
  "suggestion":"Start the server with --allow erase (or --allow all)."}}
```

`hio mcp --http` prints to stderr:

```
MCP server listening on http://127.0.0.1:47800/mcp
Token (this run only): 2bQ…Xk   — set HIO_MCP_TOKEN or use --token-file to keep one
Allowed destructive tools: flash
```

## CLI

| Command | Behaviour |
|---|---|
| `hio mcp [--http] [--port N=47800] [--token-file P] [--allow LIST] [--jlink-speed K] [--auto-reconnect] [--console-file-dir P]` | MCP server, ADR-0106. `LIST` = comma-separated `flash`, `erase`, `memory-write`, `uicr`, or `all`. An unknown item → `usage.invalid_argument`. With `--json` → `usage.invalid_argument` ("`mcp` writes JSON-RPC; `--json` does not apply"). `--token-file P`: read the token (first line, 43 base64url chars), or create it with owner-only permissions if missing (ADR-0102). Exit: 0 on stdin EOF (stdio) / shutdown, 130 on Ctrl+C, 3 `mcp.port_in_use`, 2 usage. |
| `hio chester app\|lte status` | `target_status`: halted, core id, J-Link device name. `--json`: `{"halted","coreId","device"}`. |
| `hio chester app\|lte halt`, `… go` | Halt / resume the core without a reset. |
| `hio chester app\|lte regs` | Core registers (the target must be halted; else `target.not_halted`, exit 6). `--json`: `{"registers":{"R0":"0x…",…}}`. |
| `hio chester app\|lte zones` | J-Link memory zones. |
| `hio chester app\|lte mem read ADDR [-l LEN=64] [-w 8\|16\|32] [-o FILE]` | Hexdump, or raw bytes to FILE (≤ 16 MiB; must not exist). |
| `hio chester app\|lte mem write ADDR (HEX \| -i FILE) [-w 8\|16\|32] [--yes]` | RAM / peripherals only. Asks [y/N] (ADR-0038 rules: TTY or `--yes`). |
| `hio chester app\|lte flash-write ADDR (HEX \| -i FILE) [--allow-uicr] [--yes]` | Raw bytes into flash (≤ 1 MiB). Asks like `erase`. |

The CLI is English only (ADR-0027). The existing `chester app command` is unchanged.

## Design

### Modules

```
libs/mcp/         hio_mcp (QtCore, QtNetwork)  include/hio/mcp/*.h  src/*  tests/*  fuzz/http_parser_fuzz.cpp
                  json_rpc.{h,cpp}  protocol.h (revision strings, method names, error numbers)
                  dialect_legacy.{h,cpp}  dialect_modern.{h,cpp}  server.{h,cpp}  registry.{h,cpp}
                  tool_call.{h,cpp}  schema.{h,cpp} (JSON Schema builders + argument validation)
                  stdio_transport.{h,cpp}  http_server.{h,cpp}  http_parser.{h,cpp}  token.{h,cpp}
                  device_provider.h  consent.h  activity.h
                  tools/{discovery,console,session,target,firmware,diag}_tools.cpp  prompts.cpp  resources.cpp
libs/device/      + target operations (operation.h kinds, device_session.h API, src/worker_target_ops.cpp)
                    profile.h (+ram, peripherals, shellPrompt); profiles/chester.cpp; prompt detection
libs/hw/jlink/    + jlink_abi.h declarations, IJLinkApi methods, FakeJLink scenario ops
libs/core/        + error codes, lcMcp ("hio.mcp")
app/cli/src/      mcp_command.{h,cpp}, cli_device_provider.{h,cpp}, flag_consent_broker.{h,cpp},
                  stdin_reader.{h,cpp}; target_commands.cpp (status/halt/go/regs/zones/mem/flash-write)
tests/mcp/        golden JSON exchanges (both dialects), CLI e2e drivers
```

`hio_mcp` is created with `hio_add_backend_library` (static, ADR-0022). The ADR-0004 link check applies: Core and
Network only.

### Protocol core (`hio::mcp`)

```cpp
enum class Dialect : std::uint8_t { Legacy /*2025-11-25*/, Modern /*2026-07-28*/ };
struct ClientInfo { std::string name; std::string version; };
struct RequestContext {
    Dialect dialect; ClientInfo client; std::string clientKey;   // legacy: session id; modern: clientInfo.name
    std::optional<QJsonValue> progressToken;
};

class ToolCall : public QObject {          // one in-flight call, owner thread, completes exactly once
    Q_OBJECT
public:
    void cancel();                          // idempotent; the handler stops, finish(cancelled) follows
signals:
    void progress(double done, std::optional<double> total, QString message);
    void finished(const hio::Result<hio::mcp::ToolResult>&);
};
struct ToolResult { QJsonObject structured; QString text; bool isError = false; };

struct ToolSpec {
    std::string_view name, title, description;   // English (ADR-0103)
    QJsonObject inputSchema, outputSchema;
    ToolAnnotations annotations;                 // readOnlyHint, destructiveHint, idempotentHint, openWorldHint=false
    ConsentCategories consent;                   // static part; handlers can add uicr at run time
    bool marker;                                  // ADR-0105: write a console marker
    std::function<std::unique_ptr<ToolCall>(const RequestContext&, const QJsonObject& args, ToolEnv&)> start;
};
class Registry { /* tools, resource templates, prompts; built once at server start */ };

class McpServer : public QObject {
    Q_OBJECT
public:
    McpServer(IDeviceProvider&, IConsentBroker&, IActivitySink&, ServerOptions);
    // Transport-neutral entry point: one JSON-RPC message in, zero or more messages out via `Reply`.
    void handle(const QByteArray& message, std::shared_ptr<IReplyChannel> reply, TransportInfo);
    void stop();                                  // cancels all calls, ends subscriptions
};
```

- **JSON-RPC:** single messages only (batches → `-32600`, removed since 2025-06-18). Ids are strings or integers
  and are echoed verbatim. Notifications are never answered. Parse error → `-32700`. Unknown method → `-32601`
  (HTTP 404 in the modern dialect). Invalid params → `-32602` with the JSON pointer of the first offending field.
- **Argument validation:** `schema.cpp` checks every argument against the tool's `inputSchema` subset (type,
  required, enum, min/max, maxLength, pattern for addresses `^(0x[0-9a-fA-F]{1,8}|[0-9]{1,10})$`).
  `additionalProperties: false`. Handlers receive validated values only.
- **Legacy dialect:** `initialize` (capabilities `tools`, `resources{subscribe,listChanged}`, `prompts`; server
  info `hio-monitor` + version), `notifications/initialized`, `tools/list|call`, `resources/list|templates/list|read|
  subscribe|unsubscribe`, `prompts/list|get`, `ping`, `notifications/cancelled`, `notifications/progress` (out).
- **Modern dialect:** `server/discover`, the same list/call/read/get methods with `_meta` on every request,
  `subscriptions/listen` (long-lived stream for `resources/updated` and `list_changed`), `resultType` on every
  result, and `ttlMs` (tools/prompts: 3 600 000; resources/list: 5000) with `cacheScope: "client"` on list results.
  `UnsupportedProtocolVersion` (-32022) lists `["2026-07-28","2025-11-25"]`. A header/body mismatch gives
  -32020 / HTTP 400.
- **Tool errors** (bad state, consent, device errors) are **tool results** with `isError: true` and
  `structuredContent = {"error": <ADR-0026 error object, English>}`, plus a text block "<code>: <message> — <suggestion>".
  Only protocol violations are JSON-RPC errors.
- **Output:** every tool has an `outputSchema`. Results carry `structuredContent` + one text block (compact JSON).

### Device and consent seams

```cpp
struct DeviceEntry {
    std::string id;                          // "jlink:<SN>" | "ble:<id>" (ADR-0098)
    std::string kind, displayName, profileId;
    enum class State { Open, Available, InUseByOtherProcess } state;
    std::optional<hio::device::LockOwner> owner;
    std::optional<hio::device::ConnectionEvent> connection;
    bool openedByMcp;
};
class IDeviceProvider {                      // owner thread
public:
    virtual ~IDeviceProvider() = default;
    [[nodiscard]] virtual std::vector<DeviceEntry> list() = 0;              // cheap, cached probe/BLE scan results
    [[nodiscard]] virtual hio::Status refresh() = 0;                        // one probe enumeration (≤ 2 s)
    // Opens or returns the session; async because the GUI asks for consent first.
    virtual void open(const OpenRequest&, std::function<void(hio::Result<DeviceRef>)> done) = 0;
    [[nodiscard]] virtual hio::Status close(std::string_view id, const RequestContext&) = 0;
    [[nodiscard]] virtual std::optional<DeviceRef> find(std::string_view id) = 0;
    virtual void beginBusy(std::string_view id, QString text, std::function<void()> cancel) = 0;   // ADR-0105
    virtual void endBusy(std::string_view id) = 0;
    virtual void marker(std::string_view id, QString text) = 0;
    virtual void setAiCommandHint(std::string_view id, bool active) = 0;
};
struct DeviceRef { QPointer<hio::device::ConsoleSession> console; QPointer<hio::device::DeviceSession> jlink; };

struct ConsentRequest { ClientInfo client; std::string clientKey; std::string tool; std::string deviceId;
                        ConsentCategories categories; QJsonObject details; };
class IConsentBroker { public: virtual ~IConsentBroker() = default;
    // Calls `done` exactly once. Returning a cancel functor lets a cancelled call withdraw its dialog.
    virtual std::function<void()> request(ConsentRequest, std::function<void(ConsentDecision)> done) = 0;
    virtual void clientGone(std::string_view clientKey) = 0; };
class IActivitySink { public: virtual ~IActivitySink() = default; virtual void record(ActivityEntry) = 0; };
```

- `DeviceRef` holds `QPointer`s. Every handler checks them on each step (the GUI tab can close at any time) and
  ends with `mcp.device_not_open`.
- CLI implementations: `CliDeviceProvider` owns `std::unique_ptr<ConsoleSession>`s, at most 8 (registry), each
  with its own `ConsoleLog` (100 000 lines/channel, ADR-0063) and console file (M1 rules, `--console-file-dir`).
  `FlagConsentBroker` decides immediately from `--allow`. The activity sink logs to `hio.mcp`.

### Tools (ADR-0099)

Common arguments: `device` (optional when exactly one is open, ADR-0098) and `target` (optional, defaults to the
profile's console target). All times are in seconds (numbers). Addresses are strings (`"0x20000000"` or decimal);
results print them as `0x%08X`.

| Tool | Arguments → result (structured) | Annotations / consent | Backend |
|---|---|---|---|
| `list_devices` | `refresh=false` → `{devices:[DeviceEntry]}` | ro | provider |
| `ble_scan` | `timeout=5 (1–30)`, `all=false` → `{devices:[{id,name,rssi,profile,in_use_by}]}` | ro; progress | `BleScanner` (0011) |
| `connect` | `device`, `profile?`, `target?`, `speed_khz?`, `auto_reconnect=true` → `{device, already_open, connection}` | GUI: consent `connect` | `DeviceSession::open` / `BleSession::open` (non-blocking; returns after the first attempt, ≤ 35.5 s, `waitForFirstAttempt` semantics without a nested loop) |
| `disconnect` | `device` → `{closed}` | GUI: own tabs only | provider `close` |
| `status` | `device?` (omitted = all) → `{devices:[{id, connection{state,error,target_mv}, terminal_total, terminal_buffered, log_total, log_buffered, buffer_size, busy_operation}]}` | ro | `ConsoleLog`, `connection()`, `runningOperation()` |
| `send_command` | `command (≤ 4 KiB, one line)`, `timeout=5 (0.1–120)` → ADR-0104 result | marker | `sendLine`, `linesReceived`, `promptSeen` |
| `read_terminal` | `lines=100 (1–5000)`, `after_cursor?` → `{lines:[{seq,dir,text,time}], cursor_end}` | ro | `ConsoleLog` Shell |
| `read_log` | `lines=-1`, `after_cursor?`, `pattern?` (case-insensitive, `QRegularExpression`, ≤ 512 chars, invalid → `-32602`) → `{lines:[{seq,level,text,time}], cursor_end, dropped_before}`; ≤ 5000 lines / 1 MiB per call, `truncated` | ro | `ConsoleLog` Log |
| `wait_for_state` | `pattern`, `timeout=60 (1–600)`, `command?`, `poll_interval=5 (0.5–60)`, `source=log\|terminal\|both` → rttt shape (`matched`, `matched_line`, `elapsed`, `last_lines`, cursors); a timeout is `status:"timeout"`, not an error | marker if `command`; progress | `linesReceived` |
| `wait_for_connection` | `timeout=30 (1–600)` → `{connected, state, error?, elapsed}` | ro; progress | `connectionChanged` |
| `reconnect` | `rtt_address?` (`"auto"` or address; sticky, as rttt) → `{connection}` | marker | `reconnect()` (+ new `SessionOptions::rttAddress`, see below) |
| `stop` / `start` | → `{was_reading}` / `{already_reading}` | marker | `suspend` / `resume` |
| `release_probe` / `reacquire_probe` | → `{released}` / `{connection}` | marker | new `releaseProbe` / `reacquireProbe` (below) |
| `target_status` | → `{halted, core_id, device}` | ro | `targetStatus` |
| `reset` | `halt=false` → `{halted}` | marker | `reset(target, Go\|Halt)` |
| `halt` / `go` | → `{halted}` | marker | `halt` / `go` |
| `read_memory` | `address`, `length=64 (1–65536; ≤ 16 MiB with to_file)`, `width=8\|16\|32`, `to_file?` → `{address, length, hex, dump[]}` or `{address, length, file, sha256, preview[]}` | ro | `readMemory` (chunks of 64 KiB for files) |
| `read_registers` | → `{registers:{name:"0x…"}}` | ro | `readRegisters` |
| `memory_zones` | → `{zones:[{name, description, virtual_address}]}` | ro | `memoryZones` |
| `read_pib` / `read_uicr` | → M2 `pib/uicr read` JSON | ro | M2 operations |
| `write_memory` | `address`, `data_hex? \| path?`, `width=8` → `{address, units_written}` | destructive; consent `memory-write`; marker | `writeMemory` |
| `write_flash` | `address`, `data_hex? \| path?`, `allow_uicr=false` → `{address, bytes, duration}` | destructive; `flash` (+ `uicr`); marker; progress | `writeFlash` |
| `flash` | `path? \| catalog_id?` (exactly one), `target?`, `allow_uicr=false`, `verify=true` → `{status, file\|catalog_id, target, duration, chip, events[]}` | destructive; `flash` (+ `uicr` when the image has UICR records and `allow_uicr`); marker; progress | `flash` (M2) |
| `flash_lte` | `path` (modem firmware zip or LTE bundle, ADR-0040) → M2 result | destructive; `flash`; marker; progress | `flashLteBundle` / `updateModem` |
| `erase` | `target?`, `scope=code\|all` → `{scope, duration}` | destructive; `erase`; marker; progress | `erase` |
| `recover` | `target?` → `{duration}` | destructive; `erase`; marker; progress | `recover` |
| `diagnose` | `device`, `target?` → `{checks:[CheckResult]}` | ro; progress | `diagnose` (0007) |
| `doctor` | → `{checks:[CheckResult]}` | ro; progress | `CheckRunner` (0007) |

- **Flash behaviour as rttt:** after a successful `flash`, `flash_lte`, `write_flash`, `erase` or `recover`, the
  tool appends a marker and returns new cursors. The `ConsoleLog` is **not** cleared (the GUI user still sees the
  history), so the AI uses `after_cursor` from the result to read only post-flash output (documented in the tool
  description and the prompt).
- **Busy:** an operation tool on a session that already runs one returns `internal.busy` with the running
  operation's kind. Console tools work during operations (RTT is paused by the worker, so the result reports
  `paused_by_operation`).
- **BLE devices:** console, wait, status, reconnect and disconnect tools work. Target, firmware, stop/start and
  probe tools return `mcp.capability_missing` ("This tool needs a J-Link connection; the device is connected over
  Bluetooth.").
- **Stalls:** operation tools end with `timeout` when no progress event arrives for 60 s (rttt), and cancel the
  operation first.

### New `libs/device` operations (ADR-0099)

```cpp
// device_session.h — all on the worker, one operation at a time, Result<OperationId> + operationFinished
[[nodiscard]] Result<OperationId> halt(std::string_view targetId);
[[nodiscard]] Result<OperationId> go(std::string_view targetId);
[[nodiscard]] Result<OperationId> targetStatus(std::string_view targetId);     // result.data: TargetStatus
[[nodiscard]] Result<OperationId> readMemory(std::string_view targetId, std::uint32_t address,
                                             std::size_t size, AccessWidth width = AccessWidth::Bits8);
[[nodiscard]] Result<OperationId> writeMemory(std::string_view targetId, std::uint32_t address,
                                              std::vector<std::byte> data, AccessWidth width);   // ≤ 64 KiB
[[nodiscard]] Result<OperationId> readRegisters(std::string_view targetId);    // target.not_halted
[[nodiscard]] Result<OperationId> memoryZones(std::string_view targetId);
[[nodiscard]] Result<OperationId> writeFlash(std::string_view targetId, std::uint32_t address,
                                             std::vector<std::byte> data, bool allowUicr);       // ≤ 1 MiB
[[nodiscard]] Status releaseProbe();     // suspend RTT + close the J-Link connection, keep registry slot + lock
[[nodiscard]] Status reacquireProbe();   // reopen + resume; probe.in_use_by_other_process when taken
```

- `OperationKind` gains `Halt`, `Go`, `TargetStatus`, `WriteMemory`, `ReadRegisters`, `MemoryZones` and
  `WriteFlash`. `OperationResult` gains `std::variant<…>` payloads (`TargetStatus{halted, coreId, deviceName}`,
  `RegisterSet` (≤ 128 entries), `MemoryZoneList` (≤ 32)).
- `releaseProbe` keeps the `ProbeLock`: rttt's use case is an external `JLinkExe`/`west flash`, and the lock only
  binds HARDWARIO programs. This is documented in the tool description.
- `SessionOptions::rttAddress` (`std::optional<std::uint32_t>`) plus `setRttAddress` (sticky) passes the control
  block address to `JLINK_RTTERMINAL_Control(START)` (rttt `block_address`).
- `writeFlash`: reset + halt, `IProgrammer` write with erase of the touched pages, verify, reset, RTT re-attach. It
  rejects ranges outside `flash` (and `uicr` without `allowUicr`) with `target.address_out_of_range`, and data
  touching the PIB with `firmware.uicr_in_image` unless allowed (M2 rule).
- `writeMemory`: only `ram` and `peripherals` ranges; units little-endian; size a multiple of the width.
- JLinkARM declarations added to `jlink_abi.h` (verified against pylink 2.0.2): `JLINKARM_Halt`, `JLINKARM_Go`,
  `JLINKARM_IsHalted`, `JLINKARM_GetId`, `JLINKARM_WriteMem`, `JLINKARM_WriteU16`, `JLINKARM_WriteU32`,
  `JLINKARM_ReadMemU16`, `JLINKARM_ReadMemU32`, `JLINKARM_GetRegisterList`, `JLINKARM_GetRegisterName`,
  `JLINKARM_ReadRegs`, `JLINK_GetMemZones`. Fake scenarios gain the matching ops.

### Profile additions

- `TargetProfile::ram`, `TargetProfile::peripherals` (`firmware::AddressRange`): CHESTER app (nRF52840) RAM
  `0x20000000–0x2003FFFF`, peripherals `0x40000000–0x5FFFFFFF` (+ `0xE0000000–0xE00FFFFF` PPB); lte (nRF9160)
  RAM `0x20000000–0x2003FFFF`, peripherals `0x40000000–0x5FFFFFFF` + PPB. Values are checked against the Nordic
  product specifications in phase B.
- `TargetProfile::shellPrompt`: empty until confirmed on hardware (open question 25). Prompt detection in the RTT
  line path matches the ADR-0082 rule, and `promptSeen` is emitted on the session. `ConsoleSession`'s docs change
  from "BLE only" to "when the profile has a prompt".

### HTTP transport

- `HttpServer` (`QTcpServer` on 127.0.0.1:port) → `HttpConnection` (one `QTcpSocket`, incremental `HttpParser`).
  Only `POST /mcp` (both dialects), `GET /mcp` (legacy notification stream), `DELETE /mcp` (legacy session end) and
  `OPTIONS` → 405. Any other path → 404. HTTP/1.0 → 505. `Transfer-Encoding: chunked` requests → 411.
- Check order per request: Host (421) → Origin (403) → token (401/429) → size limits (413/431) → Content-Type
  `application/json` (415) → Accept (must include `application/json` and/or `text/event-stream`, else 406) →
  dialect headers (`MCP-Protocol-Version`, modern `Mcp-Method`/`Mcp-Name`, legacy `Mcp-Session-Id`; 400) → JSON-RPC.
- Responses: `application/json` for calls that finish in under 250 ms and have no progress token. Otherwise
  `text/event-stream` (if accepted) with progress notifications and the final response, and a comment heartbeat
  every 15 s. Closing the stream cancels the call (modern). Slow-client limit 1 MiB.
- Limits and timeouts: ADR-0102. `TokenStore` (load/create/regenerate, permission fix-up, constant-time compare)
  lives in `token.{h,cpp}` and is used by the CLI and 0014.

### stdio transport and `hio mcp`

- `StdinReader` (`std::jthread`): reads stdin in blocks, splits lines (≤ 4 MiB; longer → JSON-RPC parse error, rest
  of the line dropped), and pushes them into a `BoundedQueue<QByteArray>(64)`; when full, the reader waits, so
  there is backpressure. A queued signal wakes the owner thread. EOF → `stdinClosed`. On Windows, stop uses
  `CancelSynchronousIo` on the reader thread; on POSIX, the reader polls with a 100 ms timeout and checks
  `stop_requested`.
- stdout: one JSON message + `\n` per write, flushed, owner thread only. Nothing else is ever written to stdout
  (logging goes to the log file and stderr).
- `mcp_command.cpp`: builds `CliDeviceProvider`, `FlagConsentBroker`, `McpServer`, the transport(s), then
  `QCoreApplication::exec()`. SIGINT/SIGTERM (existing `SignalGuard`) → `server.stop()` → close sessions (≤ 5 s) →
  `quit()` → exit 130.

### Error codes added (texts translatable, ADR-0027; rendered in English over MCP, ADR-0103)

| Code | Category | Message / cause / suggestion |
|---|---|---|
| `mcp.port_in_use` | Io | "The MCP port %1 is already in use." / another HARDWARIO Monitor, `hio mcp --http` or rttt / "Stop the other server or choose another port." |
| `mcp.listen_failed` | Io | "The MCP server could not start on 127.0.0.1:%1." / %2 (OS error) / "Choose another port." |
| `mcp.token_file` | Io | "The MCP token file %1 cannot be read or created." / permissions / "Check the file permissions or delete the file to create a new token." |
| `mcp.tool_not_allowed` | Usage | "The tool %1 is not allowed by this server." / not in `--allow`, or a tab opened by the user / "Start the server with --allow %2 (or --allow all)." (GUI variant: "Close the tab in HARDWARIO Monitor yourself.") |
| `mcp.consent_denied` | Cancelled | "The user did not allow %1." / — / "Ask the user to allow it in HARDWARIO Monitor." |
| `mcp.consent_timeout` | Timeout | "Nobody answered the permission request for %1 within 2 minutes." / — / "Ask the user to watch HARDWARIO Monitor and try again." |
| `mcp.device_ambiguous` | Usage | "Several devices are open; say which one." / — / "Pass device: one of %1." |
| `mcp.device_not_open` | Usage | "The device %1 is not connected." / closed, never opened, or its tab was closed / "Call connect first." |
| `mcp.capability_missing` | Usage | "This tool needs a J-Link connection; %1 is connected over Bluetooth." / — / "Connect the device with a J-Link probe." |
| `mcp.too_many_requests` | Internal | "Too many MCP requests are running (limit 32)." / — / "Wait for running calls to finish." |
| `target.not_halted` | Target | "The CPU of %1 is running." / registers can be read only from a halted core / "Call halt first." |
| `target.address_out_of_range` | Target | "Address range %1–%2 is not allowed for this operation." / outside RAM/peripherals (write) or flash (program) / "Use an address inside %3." |
| `target.memory_access_failed` | Target | "Reading or writing memory at %1 failed." / address not mapped, core in a fault state / "Check the address; try reset." |
| `io.file_exists` | Io | "The file %1 already exists." / — / "Choose a new file name." |

Protocol-level HTTP refusals (401/403/421/413/…) are not `hio::Error`s. They are logged in `hio.mcp`, with the
token never logged.

### Threading summary

Owner thread: `McpServer`, dialects, transports, `HttpServer`/connections, all `ToolCall`s, provider, consent
broker, `ConsoleLog` reads. Session worker threads: unchanged, plus the new operations. The CLI stdin reader is
one `std::jthread`, with only the bounded line queue crossing threads (ADR-0097).

### Bounds

In-flight calls 32 · HTTP connections 16, headers 16 KiB, body 4 MiB, header timeout 10 s, body 30 s, idle 60 s,
per-connection send queue 1 MiB · legacy sessions 16, idle 30 min · listen streams 16 · subscriptions 64,
updates ≤ 1/s · progress ≤ 4/s · stdio line 4 MiB, stdin queue 64 lines · auth failures 10/60 s → 10 s lockout ·
`send_command` timeout 0.1–120 s, output 2000 lines / 256 KiB · `read_log`/`read_terminal` ≤ 5000 lines / 1 MiB ·
`wait_for_*` ≤ 600 s · regex ≤ 512 chars · `read_memory` 64 KiB inline, 16 MiB to file · `write_memory` 64 KiB ·
`write_flash` 1 MiB · image files 64 MiB · operation stall 60 s · sessions 8 · activity entries 1000 (sink) ·
registers 128, zones 32.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Next milestone to plan | M6 / M7 / M8 | M6 MCP | — |
| Where to write | commit M5 + new branch / in m5 worktree / copy | M5 committed (290c315), `v2/m6-plan` in `../hio-monitor-m6-plan` | — |
| M6 split | two / three / one | two: core + tools + CLI (0013), GUI (0014) | ADR-0094 |
| Protocol revision | both / 2025-11-25 / 2026-07-28 | both (dual-era) | ADR-0095 |
| SDK | own on QtNetwork / cxxmcp / gopher-mcp | own on QtNetwork + QJsonDocument | ADR-0096 |
| Threading | owner thread async / own thread | owner thread, async handlers, no nested loops | ADR-0097 |
| Device addressing | multi-device ids + connect / one device / GUI tabs only | multi-device, stable ids, `connect` | ADR-0098 |
| Tool set | rttt parity + ours / no low-level writes / read-only | rttt parity + ours, new backend operations | ADR-0099 |
| Images | path + catalog / + upload / + base64 | local absolute path or catalog id | ADR-0100 |
| GUI consent | ask + remember per client / always ask / settings only | ask, remember per client | ADR-0101 |
| CLI consent | `--allow` flags / all allowed / elicitation | `--allow` flags | ADR-0101 |
| Token storage | 0600 file / INI / keychain | separate owner-only file | ADR-0102 |
| Port | 47800 fixed / 8090 / next free | 47800, error if busy | ADR-0102 |
| MCP language | English / GUI language | English always (amends ADR-0027) | ADR-0103 |
| `send_command` end | prompt then silence / silence / fixed window | prompt, then 300 ms silence, absolute timeout | ADR-0104 |
| Shared shell | no lock / lock input | no lock, echo-based, `interleaved` flag | ADR-0105 |
| Activity display | markers + panel / panel / markers | console markers + MCP panel + rail indicator | ADR-0105 |
| CLI HTTP | `--http` / stdio only | `--http` opt-in | ADR-0106 |
| Extras | prompt / progress / resources / tasks | `debug_device` prompt, progress + cancel, resources; no tasks | ADR-0107 |
| Minor defaults: all bounds and timeouts above; tool names (rttt names kept, `jlink_close/open` → `release_probe/reacquire_probe`); `read_memory` `to_file` never overwrites; ConsoleLog not cleared after flash (cursors instead); BLE gets `mcp.capability_missing` for J-Link tools; `releaseProbe` keeps the lock; error codes and categories above; CHESTER RAM/peripheral ranges (checked in phase B); new CLI commands `status/halt/go/regs/zones/mem/flash-write`; `lcMcp` = `hio.mcp`; JSON for 250 ms calls, SSE otherwise | accept / change | _pending_ | — |

## Implementation steps

Phase A — protocol core
- [ ] A0 Prerequisite check: M1 (0002) and M2 (0003) implemented (otherwise stop and report)
- [ ] A1 CMake: `hio_mcp` target, `lcMcp`, error codes in `libs/core` (+ `cs` translations)
- [ ] A2 `json_rpc` + `protocol.h` + `schema` (builders, validation) + tests
- [ ] A3 `Registry`, `ToolCall`, `McpServer` with both dialects (handshake, discover, list/call/read/get, errors,
  progress, cancellation, subscriptions) + golden-exchange tests per dialect
- [ ] A4 `IDeviceProvider`, `IConsentBroker`, `IActivitySink` + mocks

Phase B — backend additions
- [ ] B1 JLinkARM declarations + `IJLinkApi` methods + pylink cross-check + fake scenario ops
- [ ] B2 `DeviceSession` operations `halt/go/targetStatus/readMemory(width)/writeMemory/readRegisters/
  memoryZones/writeFlash`, `releaseProbe/reacquireProbe`, `rttAddress` + tests (fake + GMock)
- [ ] B3 Profile `ram`/`peripherals`/`shellPrompt`, RTT prompt detection, `promptSeen` on `DeviceSession` + tests
- [ ] B4 CLI `chester app|lte status|halt|go|regs|zones|mem read|mem write|flash-write` + e2e tests

Phase C — tools and transports
- [ ] C1 Discovery, console, session-control tools (+ `send_command` per ADR-0104 with fake clock) + tests
- [ ] C2 Target and firmware tools (consent categories incl. run-time `uicr`, markers, busy, stall) + tests
- [ ] C3 Resources and the `debug_device` prompt + tests
- [ ] C4 `StdioTransport`, `StdinReader` + tests (TSan)
- [ ] C5 `HttpParser`, `HttpServer`, `TokenStore` + tests (limits, check order, SSE, keep-alive, slow client) +
  libFuzzer target `http_parser_fuzz` (ASan preset, 60 s smoke in CI)
- [ ] C6 `hio mcp [--http]`: `CliDeviceProvider`, `FlagConsentBroker`, signal handling + CLI e2e tests (both
  transports, both dialects)

Phase D — tools that depend on later plans (each skipped until its plan is implemented)
- [ ] D1 `diagnose`, `doctor` (after 0007 phase A)
- [ ] D2 BLE devices in `list_devices`/`connect`, `ble_scan` (after 0011)

Phase E — verification and docs
- [ ] E1 Conformance: official MCP conformance suite + MCP Inspector against `hio mcp --http` and stdio
  (manual; optional CI job behind `HIO_MCP_CONFORMANCE=1`, Node not required for the normal CI)
- [ ] E2 Interop (manual, by the user): Claude Code (stdio and HTTP), Claude Desktop (stdio), Cursor (HTTP, legacy)
- [ ] E3 HIL (`HIO_HIL=1`, CHESTER): flash → wait_for_connection → send_command; halt/regs/go; read/write RAM;
  write_flash into a scratch page and restore; RTT prompt recorded into the profile (open question 25)
- [ ] E4 Update CLAUDE.md "Commands", `0006-cli.md`, `0004-mcp.md`, roadmap

## Tests and verification

- **Protocol (both dialects):** handshake, version negotiation (supported, older legacy, unknown → -32022 with
  list), batch rejected, notifications not answered, ids echoed (string/int), invalid params with pointer, unknown
  tool, `resultType`/`ttlMs` present (modern), header mismatch (-32020), session id required/forbidden,
  legacy session expiry and DELETE, cancellation before/after start, progress throttling, subscription updates
  throttled, list_changed on open/close.
- **HTTP:** every check-order branch with exact status codes; Origin variants (`null`, other port, `localhost`,
  IPv6 literal); Host variants; token wrong/missing/constant-time path; lockout after 10 failures; header 16 KiB+1,
  body 4 MiB+1; chunked request; slowloris header timeout; keep-alive reuse; SSE heartbeat; stream close cancels;
  slow client disconnected at 1 MiB; 17th connection refused; port in use → `mcp.port_in_use`; fuzzing (ASan).
- **Tools (fake J-Link scenarios + mock provider/broker):** each tool's happy path and validation errors;
  `send_command` ends by prompt / silence / timeout, echo filtering, `interleaved`, output caps; `read_log` cursor
  across eviction (`dropped_before`); `wait_for_state` passive and polling, invalid regex; `flash` with path,
  catalog id, both (→ -32602), UICR image without/with `allow_uicr` (consent `uicr` requested), cancel mid-flash,
  stall 60 s (fake clock); consent denied / timeout / cancel withdraws the request; `internal.busy`; device closed
  mid-call (`QPointer`); `device` omitted with 0/1/2 sessions; BLE session → `mcp.capability_missing`.
- **Backend:** new operations against GMock'd `IJLinkApi` (call order, widths, little-endian units, halted check,
  range checks incl. edges, PIB protection, verify failure) and fake scenarios; `releaseProbe` keeps the lock;
  M1/M2 suites green unchanged.
- **CLI e2e:** `hio mcp` over stdio with scripted JSON-RPC (both dialects): initialize → list → connect → flash
  (fake) → status; `--allow` missing → `mcp.tool_not_allowed`; `--json` → exit 2; stdin EOF → exit 0, sessions
  closed; SIGINT → 130 without hang; nothing but JSON on stdout (checked line by line). `hio mcp --http` with
  curl-like requests from a Qt test client. New target commands, human and `--json`.
- **Owner-thread stalls:** with 32 in-flight `wait_for_state` calls and 10 000 lines/s from the fake, no owner-thread
  task takes more than 20 ms (measured in the test).
- **Sanitizers / static analysis / translations:** ASan+UBSan (incl. fuzz smoke), TSan (stdin reader, sessions),
  clang-tidy, clang-format, `.ts` freshness.

## Risks and open points

- **Spec churn:** 2026-07-28 is new and clients are still migrating. Both dialects are isolated adapters. Interop is
  checked manually before Done (E2); differences found there need a plan update and the user's approval.
- **Client timeouts:** long flashes (LTE modem update, minutes) can exceed some clients' tool timeouts. Progress
  notifications help where clients reset their timeout on progress; the tool descriptions state typical durations.
- **CHESTER RTT prompt** is unknown (open question 25). Until it is confirmed, `send_command` uses the silence rule.
- **New JLinkARM functions** need the ABI cross-check (pylink 2.0.2, ADR-0028) and HIL. `JLINK_GetMemZones` returns
  nothing for some devices; the tool then returns an empty list.
- **Security of a localhost server:** other local users can reach 127.0.0.1. The token protects that; the token
  file permissions are verified on every platform in E2.
- **Own HTTP parser** is new attack surface: fuzzing plus strict limits, and no features beyond what MCP needs.

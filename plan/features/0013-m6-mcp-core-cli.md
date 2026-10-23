# 0013 — M6 MCP core, tools and CLI: `libs/mcp`, `libs/mcp_tools`, low-level target operations, `hio mcp`

- **Status:** In progress (approved 2026-10-20 by Matej Strnad, revised text incl. ADR-0150–0156 and the minor
  defaults; implementation on the M6 worktree branch from `v2/m5-ble` 7dc190a, phases A–C first)
- **Milestone:** M6 (plan 1 of 2, ADR-0094; followed by [0014](0014-m6-mcp-gui.md))
- **Author:** Matej Strnad (with Claude), 2026-10-16; revised 2026-10-20 to match the M1–M5 code on `v2/m5-ble`
  (7dc190a)
- **Related ADRs:** ADR-0004, ADR-0007, ADR-0008, ADR-0019, ADR-0020, ADR-0022, ADR-0024, ADR-0026, ADR-0027,
  ADR-0028, ADR-0031, ADR-0035–ADR-0040, ADR-0053, ADR-0057, ADR-0058, ADR-0063, ADR-0068, ADR-0069, ADR-0079,
  ADR-0082, ADR-0083, ADR-0094–ADR-0107, ADR-0127, ADR-0129, ADR-0141, ADR-0150–ADR-0164
- **Depends on:** 0002 (M1), 0003 (M2), [0007](0007-m4-wizard-framework-diagnostics.md) phase A (`libs/diag`,
  `Diagnose`) and [0011](0011-m5-ble-backend-cli.md) (`ConsoleSession`, `BleSession`, `BleScanner`). All are on
  `v2/m5-ble`; 0011 is still being finished there (review fixes, HIL). Phases A–C do not depend on the BLE code
  beyond the `ConsoleSession` interface. Phase D (diagnosis and BLE tools) starts only after the user says M5 has
  settled; until then those tools are not registered.

> **Revision 2026-10-20 (before approval):** the text was synchronised with the code after M1–M5 (`DeviceSession`,
> `ConsoleSession`, `ConsoleLog`, `SessionRegistry`/`SessionKey`, `IJLinkApi`/`jlink_abi.h`, the fake J-Link,
> `IProgrammer`, CLI structure, error-text rules, `HIO_TEST_OVERRIDES`). New decisions: two libraries (ADR-0150),
> optional J-Link symbols (ADR-0151), `linux-fuzz` preset (ADR-0152), `mem write`/`flash-write` confirmation
> (ADR-0153), shared stdin reader (ADR-0154), `ConsoleLog` totals (ADR-0155), `--console-file-dir` (ADR-0156). The
> English rendering of backend errors in the GUI server (ADR-0103) is deferred to 0014 (the CLI is English already).
> Every difference found is listed in "Sync with the code" at the end.

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
1. `libs/mcp` (`hio_mcp`, QtCore + QtNetwork + `hio::core` only, ADR-0150): the JSON-RPC 2.0 core, both protocol
   dialects, the tool / resource / prompt registry, the stdio transport, a bounded HTTP/1.1 transport and the token
   store. Seams `IConsentBroker` and `IActivitySink`.
2. `libs/mcp_tools` (`hio_mcp_tools`, ADR-0150): `IDeviceProvider`, all v1 tools (ADR-0099), the `debug_device`
   prompt, the resources, progress and cancellation (ADR-0107).
3. `libs/device`: new operations `halt`, `go`, `targetStatus`, `writeMemory`, `readMemory` with width,
   `readRegisters`, `memoryZones` and `writeFlash`; `releaseProbe`/`reacquireProbe`; a sticky RTT control-block
   address; the JLinkARM functions they need (optional, ADR-0151); profile additions `ram`, `peripherals` and
   `shellPrompt` with RTT prompt detection (ADR-0104); `ConsoleLog` totals (ADR-0155); `consoleFileDir`
   (ADR-0156).
4. CLI: `hio mcp [--http]` (ADR-0106) and the CLI commands for the new operations (ADR-0099, ADR-0153).
5. Every error is an `hio::Error` following the existing text rules (no placeholders in table defaults). Every
   buffer, queue and timeout is bounded and documented.

**Non-goals**
- GUI: tabs as devices, consent dialogs, settings, token UI, markers, activity list, indicator (0014). English
  rendering of translated backend errors for the GUI server (0014, see ADR-0103 note there).
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

A tool error the AI receives (tool result with `isError: true`; the error object is `hio::toJson(Error)`, ADR-0026):

```json
{"error":{"code":"mcp.tool_not_allowed","category":"usage",
  "message":"The tool erase is not allowed by this server.",
  "cause":"Destructive tools must be allowed when the server starts.",
  "suggestion":"Start the server with --allow erase (or --allow all).","detail":""}}
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
| `hio mcp [--http] [--port N=47800] [--token-file P] [--allow LIST] [--jlink-speed K] [--auto-reconnect] [--console-file-dir DIR]` | MCP server, ADR-0106. Global options (`--jlink-library`, `--nrfutil`, `--log-level`) apply. `LIST` = comma-separated `flash`, `erase`, `memory-write`, `uicr`, or `all`; an unknown item → `usage.invalid_argument`. With `--json` → `usage.invalid_argument` ("`mcp` writes JSON-RPC; `--json` does not apply"). `--token-file P`: read the token (first line, 43 base64url chars), or create it with owner-only permissions if missing (ADR-0102); `HIO_MCP_TOKEN` (new production env var, not under `HIO_TEST_OVERRIDES`) overrides both. `--console-file-dir DIR` (ADR-0156): must exist and be writable, else the command fails before serving. `--jlink-speed`/`--auto-reconnect` are the defaults for `connect`. The command returns `streamed=true`, so `main` never prints an envelope to stdout. Exit: 0 on stdin EOF (stdio) / shutdown, 130 on Ctrl+C, 3 `mcp.port_in_use`/`mcp.listen_failed`/`mcp.token_file`, 2 usage. |
| `hio chester app\|lte status` | `targetStatus`: halted, core id, J-Link device name. `--json`: `{"halted","coreId","device"}`. |
| `hio chester app\|lte halt`, `… go` | Halt / resume the core without a reset. |
| `hio chester app\|lte regs` | Core registers (the target must be halted; else `target.not_halted`, exit 6). `--json`: `{"registers":{"R0":"0x…",…}}`. |
| `hio chester app\|lte zones` | J-Link memory zones (empty list when the library reports none). |
| `hio chester app\|lte mem read ADDR [-l LEN=64] [-w 8\|16\|32] [-o FILE]` | Hexdump, or raw bytes to FILE (≤ 16 MiB; the file must not exist → `io.file_exists`; unlike `uicr read -o`, which keeps overwriting). |
| `hio chester app\|lte mem write ADDR (HEX \| -i FILE) [-w 8\|16\|32] [--yes]` | RAM / peripherals only. Asks [y/N] (ADR-0153: TTY or `--yes`). |
| `hio chester app\|lte flash-write ADDR (HEX \| -i FILE) [--allow-uicr] [--yes]` | Raw bytes into flash (≤ 1 MiB). Asks [y/N] (ADR-0153). |

All new `chester` commands take the existing per-group probe options (`-n/--jlink-sn`, `--jlink-speed`) and open a
flash-style session (`attachRtt=false`) through the operation runner that `flash_commands.cpp` uses today; that
helper moves into a shared `app/cli/src/operation_runner.{h,cpp}`. The CLI is English only (ADR-0027). The existing
`chester app command` is unchanged. There is no `lte` RTT console (the lte target has no RTT terminal).

## Design

### Modules

```
libs/mcp/         hio_mcp (QtCore, QtNetwork, hio::core; ADR-0150)  include/hio/mcp/*.h  src/*  tests/*
                  json_rpc.{h,cpp}  protocol.h (revision strings, method names, error numbers)
                  dialect_legacy.{h,cpp}  dialect_modern.{h,cpp}  server.{h,cpp}  registry.{h,cpp}
                  tool_call.{h,cpp}  schema.{h,cpp} (JSON Schema builders + argument validation)
                  limits.h (all bounds and timeouts, injectable for tests)
                  stdio_transport.{h,cpp}  http_server.{h,cpp}  http_parser.{h,cpp}  token.{h,cpp}
                  consent.h  activity.h
                  fuzz/http_parser_fuzz.cpp  fuzz/json_rpc_fuzz.cpp  fuzz/corpus/*   (linux-fuzz only, ADR-0152)
libs/mcp_tools/   hio_mcp_tools (hio_mcp, hio::device, hio::firmware; + hio::diag in phase D)
                  include/hio/mcp/tools/*.h  src/*  tests/*
                  device_provider.h  tools/{discovery,console,session,target,firmware,diag}_tools.cpp
                  prompts.cpp  resources.cpp  register_tools.cpp
libs/device/      + operation.h (kinds, AccessWidth, payload types), device_session.h API,
                    src/session_worker_target.cpp (free functions over `IProgrammer`, ADR-0158, dispatched from the
                    existing switch in session_worker.cpp), profile.h (+ram, peripherals, shellPrompt),
                    profiles/chester.cpp, RTT prompt detection, ConsoleLog totals, consoleFileDir
libs/hw/jlink/    + jlink_abi.h declarations, optional symbol resolution, IJLinkApi methods,
                    MockJLinkApi (tests) — plus libs/device/tests/scripted_api.h
libs/core/        + error codes, lcMcp ("hio.mcp")
tests/fake_jlink/ + halted state, RAM/peripheral memory model, registers, zones, RTT block address, new exports
app/cli/src/      mcp_command.{h,cpp}, cli_device_provider.{h,cpp}, flag_consent_broker.{h,cpp},
                  operation_runner.{h,cpp} (moved from flash_commands.cpp), target_commands.cpp
                  (status/halt/go/regs/zones/mem/flash-write); terminal.h StdinLineReader parametrised (ADR-0154)
app/cli/tests/    CLI e2e drivers for `hio mcp` (stdio + HTTP) and the target commands (cli_process.h pattern)
```

Golden JSON exchanges (both dialects) live in `libs/mcp/tests/golden/` and `libs/mcp_tools/tests/golden/`. Tests use
plain `add_executable` + `hio_apply_target_defaults` + `gtest_discover_tests(... LABELS "unit")` as the other libs.
Tests that need the fake J-Link are added only with `HIO_TEST_OVERRIDES` on (as `app/cli/tests`).

Both libraries are created with `hio_add_backend_library` (static, ADR-0022; the ADR-0004 GUI-module denylist
applies). In addition, a configure-time check fails if `hio_mcp` links any `hio::` target other than `hio::core`.

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
    McpServer(Registry, IConsentBroker&, IActivitySink&, ServerOptions);   // ServerOptions holds Limits
    // Transport-neutral entry point: one JSON-RPC message in, zero or more messages out via `reply`.
    void handle(const QByteArray& message, std::shared_ptr<IReplyChannel> reply, TransportInfo);
    void stop();                                  // cancels all calls, ends subscriptions
};
```

`ToolEnv` is an opaque per-server context that `hio_mcp_tools` fills (device provider, consent broker, activity sink);
`hio_mcp` itself never sees device types.

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
  result, and `ttlMs` (tools/prompts: 3 600 000; resources/list: 5000) with `cacheScope: "private"` on list results; `server/discover` and
  `resources/templates/list` (3 600 000) and `resources/read` (`ttlMs` 0) carry both fields too (2026-07-28 caching
  rules; `"client"` is not a valid value).
  `UnsupportedProtocolVersion` (-32022) lists `["2026-07-28","2025-11-25"]`. A header/body mismatch gives
  -32020 / HTTP 400.
- **Tool errors** (bad state, consent, device errors) are **tool results** with `isError: true` and
  `structuredContent = {"error": hio::toJson(Error)}` (ADR-0026: code, category, message, cause, suggestion,
  detail, wizard when set), plus a text block "<code>: <message> — <suggestion>". Only protocol violations are
  JSON-RPC errors.
- **Output:** every tool has an `outputSchema`. Results carry `structuredContent` + one text block (compact JSON).
- **Timers:** all timeouts and rates are fields of `hio::mcp::Limits` (defaults = "Bounds" below) passed in
  `ServerOptions`; owner-thread waits use `QTimer`/`QDeadlineTimer`. Tests shrink the limits instead of using a
  fake clock (there is no owner-thread clock abstraction in the code; `hio::jlink::Clock` is a blocking worker
  clock and is not reused).

### Device and consent seams

```cpp
// hio_mcp_tools: device_provider.h
struct DeviceEntry {
    std::string id;                          // "jlink:<SN>" | "ble:<id>" (ADR-0098; SessionKey ProbeKey/BleKey)
    std::string kind, displayName, profileId;
    enum class State { Open, Available, InUseByOtherProcess } state;
    std::optional<hio::device::ProbeLockOwner> owner;   // from ProbeStatus::usedByOtherProcess / BleUse::otherProcess
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
    virtual void marker(std::string_view id, QString text) = 0;               // ConsoleLog::appendMarker
    virtual void setAiCommandHint(std::string_view id, bool active) = 0;
};
struct DeviceRef { QPointer<hio::device::ConsoleSession> console; QPointer<hio::device::DeviceSession> jlink; };

// hio_mcp: consent.h / activity.h
struct ConsentRequest { ClientInfo client; std::string clientKey; std::string tool; std::string deviceId;
                        ConsentCategories categories; QJsonObject details; };
class IConsentBroker { public: virtual ~IConsentBroker() = default;
    // Calls `done` exactly once. Returning a cancel functor lets a cancelled call withdraw its dialog.
    virtual std::function<void()> request(ConsentRequest, std::function<void(ConsentDecision)> done) = 0;
    virtual void clientGone(std::string_view clientKey) = 0; };
class IActivitySink { public: virtual ~IActivitySink() = default; virtual void record(ActivityEntry) = 0; };
```

- `DeviceRef` holds `QPointer`s. Every handler checks them on each step (the GUI tab can close at any time) and
  ends with `mcp.device_not_open`. `DeviceRef::jlink` is null for BLE sessions.
- The J-Link `DeviceSession` is opened for one console target (`SessionOptions::targetId`, exposed as
  `consoleTargetId()`); operations take any target of the profile (ADR-0057). For BLE the console target is
  `BleProfile::consoleTargetId`.
- CLI implementations: `CliDeviceProvider` owns `std::unique_ptr<ConsoleSession>`s within the registry limit
  (`SessionRegistry::maxSessions` = 8, shared with BLE), each with its own `ConsoleLog` (100 000 lines/channel,
  ADR-0063) and console file (M1 rules; `consoleFileDir` from `--console-file-dir`, ADR-0156). Probe list from
  `SessionRegistry::enumerateProbes` (`ProbeStatus::usedBySession` / `usedByOtherProcess`). `FlagConsentBroker`
  decides immediately from `--allow`. The activity sink logs to `hio.mcp`.

### Tools (ADR-0099)

Common arguments: `device` (optional when exactly one is open, ADR-0098) and `target` (optional, defaults to the
session's `consoleTargetId()`). All times are in seconds (numbers). Addresses are strings (`"0x20000000"` or
decimal); results print them as `0x%08X`.

| Tool | Arguments → result (structured) | Annotations / consent | Backend |
|---|---|---|---|
| `list_devices` | `refresh=false` → `{devices:[DeviceEntry]}` | ro | provider |
| `ble_scan` (phase D) | `timeout=5 (1–30)`, `all=false` → `{devices:[{id,name,rssi,profile,in_use_by}]}` | ro; progress | `BleScanner` (`ScanOptions::duration` in ms, `BleDeviceInfo`), `in_use_by` from `SessionRegistry::bleInUse`; the provider owns the `BleHost` |
| `connect` | `device`, `profile?`, `target?`, `speed_khz?`, `auto_reconnect=true` → `{device, already_open, connection}` | GUI: consent `connect` | `DeviceSession::open` / `BleSession::open` (non-blocking; returns after the first attempt, ≤ 35.5 s, `waitForFirstAttempt` semantics re-implemented on `connectionChanged` — the existing helper runs a nested loop, ADR-0097) |
| `disconnect` | `device` → `{closed}` | GUI: own tabs only | provider `close` |
| `status` | `device?` (omitted = all) → `{devices:[{id, connection{state,error,target_mv}, terminal_total, terminal_buffered, log_total, log_buffered, buffer_size, busy_operation}]}` | ro | `ConsoleLog::total/size/capacityPerChannel` (ADR-0155), `connection()`, new `runningOperationKind()` |
| `send_command` | `command (≤ 4 KiB, one line)`, `timeout=5 (0.1–120)` → ADR-0104 result | marker | `sendLine`, `linesReceived`, `promptSeen` |
| `read_terminal` | `lines=100 (1–5000)`, `after_cursor?` → `{lines:[{seq,dir,text,time}], cursor_end, dropped_before}` | ro | `ConsoleLog::linesSince/last` Shell |
| `read_log` | `lines=-1`, `after_cursor?`, `pattern?` (case-insensitive, `QRegularExpression`, ≤ 512 chars, invalid → `-32602`) → `{lines:[{seq,level,text,time}], cursor_end, dropped_before}`; ≤ 5000 lines / 1 MiB per call, `truncated` | ro | `ConsoleLog` Log, `firstSeq` (ADR-0155) |
| `wait_for_state` | `pattern`, `timeout=60 (1–600)`, `command?`, `poll_interval=5 (0.5–60)`, `source=log\|terminal\|both` → rttt shape (`matched`, `matched_line`, `elapsed`, `last_lines`, cursors); a timeout is `status:"timeout"`, not an error | marker if `command`; progress | `linesReceived` |
| `wait_for_connection` | `timeout=30 (1–600)` → `{connected, state, error?, elapsed}` | ro; progress | `connectionChanged` |
| `reconnect` | `rtt_address?` (`"auto"` or address; sticky, as rttt) → `{connection}` | marker | `setRttAddress` + `reconnect()` |
| `stop` / `start` | → `{was_reading}` / `{already_reading}` | marker | `suspend` / `resume` |
| `release_probe` / `reacquire_probe` | → `{released}` / `{connection}` | marker | new `releaseProbe` / `reacquireProbe` (below) |
| `target_status` | → `{halted, core_id, device}` | ro | `targetStatus` |
| `reset` | `halt=false` → `{halted}` | marker | `reset(target, ResetMode::Go\|Halt)` |
| `halt` / `go` | → `{halted}` | marker | `halt` / `go` |
| `read_memory` | `address`, `length=64 (1–65536; ≤ 16 MiB with to_file)`, `width=8\|16\|32`, `to_file?` → `{address, length, hex, dump[]}` or `{address, length, file, sha256, preview[]}` | ro | `readMemory` (chunks of 64 KiB for files; `to_file` never overwrites → `io.file_exists`) |
| `read_registers` | → `{registers:{name:"0x…"}}` | ro | `readRegisters` |
| `memory_zones` | → `{zones:[{name, description, virtual_address}]}` | ro | `memoryZones` |
| `read_pib` / `read_uicr` | → M2 `pib/uicr read` JSON (app target only, as the CLI) | ro | M2 operations |
| `write_memory` | `address`, `data_hex? \| path?`, `width=8` → `{address, units_written}` | destructive; consent `memory-write`; marker | `writeMemory` |
| `write_flash` | `address`, `data_hex? \| path?`, `allow_uicr=false` → `{address, bytes, duration}` | destructive; `flash` (+ `uicr`); marker; progress | `writeFlash` |
| `flash` | `path? \| catalog_id?` (exactly one), `target?`, `allow_uicr=false`, `verify=true` → `{status, file\|catalog_id, target, duration, chip}` | destructive; `flash` (+ `uicr` when the image has UICR records and `allow_uicr`); marker; progress | `flash` (M2) |
| `flash_lte` | `path` (modem firmware zip or LTE bundle, ADR-0040) → M2 result | destructive; `flash`; marker; progress | `flashLteBundle` / `updateModem` |
| `erase` | `target?`, `scope=code\|all` → `{scope, duration}` | destructive; `erase`; marker; progress | `erase` |
| `recover` | `target?` → `{duration}` | destructive; `erase`; marker; progress | `recover` |
| `diagnose` (phase D) | `device`, `target?` → `{checks:[{id,title,status,result,error?}]}` | ro; progress | `diagnose` (`OperationResult::checks` = `DiagnoseStep`, titles from `diagnoseSteps(target)`) |
| `doctor` (phase D) | → `{checks:[…]}` (same shape as `hio doctor --json`, builder moved from the CLI into `libs/diag`) | ro; progress | `CheckRunner::run` on an owned `std::jthread` (it blocks up to 16 × 15 s), result posted to the owner thread |

- **Flash behaviour as rttt:** after a successful `flash`, `flash_lte`, `write_flash`, `erase` or `recover`, the
  tool appends a marker and returns new cursors. The `ConsoleLog` is **not** cleared (the GUI user still sees the
  history), so the AI uses `after_cursor` from the result to read only post-flash output (documented in the tool
  description and the prompt).
- **Busy:** an operation tool on a session that already runs one returns `internal.busy` whose detail names the
  running operation's kind (`operationKindId(runningOperationKind())`). Console tools work during operations (RTT is
  paused by the worker, so the result reports `paused_by_operation`).
- **BLE devices:** console, wait, status, reconnect and disconnect tools work. Target, firmware, stop/start and
  probe tools return `mcp.capability_missing`.
- **Stalls:** operation tools end with `timeout` when no `operationProgress` arrives for 60 s (rttt), and cancel
  the operation first (`DeviceSession::cancel`).

### New `libs/device` operations (ADR-0099)

```cpp
// operation.h
enum class AccessWidth : std::uint8_t { Bits8 = 1, Bits16 = 2, Bits32 = 4 };
struct TargetStatus { bool halted; std::uint32_t coreId; std::string deviceName; };
struct RegisterValue { std::string name; std::uint32_t value; };          // ≤ 128 per result
struct MemoryZone { std::string name; std::string description; std::uint64_t virtualAddress; };   // ≤ 32
// OperationKind gains: Halt, Go, TargetStatus, WriteMemory, ReadRegisters, MemoryZones, WriteFlash
// OperationResult gains per-kind fields (existing pattern, like `data` and `checks`):
//   std::optional<TargetStatus> targetStatus; std::vector<RegisterValue> registers; std::vector<MemoryZone> zones;
//   (ReadMemory keeps `data`; Halt/Go set targetStatus.halted)

// device_session.h — all on the worker, one operation at a time, Result<OperationId> + operationFinished
[[nodiscard]] Result<OperationId> halt(std::string_view targetId);
[[nodiscard]] Result<OperationId> go(std::string_view targetId);
[[nodiscard]] Result<OperationId> targetStatus(std::string_view targetId);
[[nodiscard]] Result<OperationId> readMemory(std::string_view targetId, std::uint32_t address,
                                             std::size_t size, AccessWidth width = AccessWidth::Bits8);  // existing + width
[[nodiscard]] Result<OperationId> writeMemory(std::string_view targetId, std::uint32_t address,
                                              std::vector<std::byte> data, AccessWidth width);   // ≤ 64 KiB
[[nodiscard]] Result<OperationId> readRegisters(std::string_view targetId);    // target.not_halted
[[nodiscard]] Result<OperationId> memoryZones(std::string_view targetId);
[[nodiscard]] Result<OperationId> writeFlash(std::string_view targetId, std::uint32_t address,
                                             std::vector<std::byte> data, bool allowUicr);       // ≤ 1 MiB
[[nodiscard]] std::optional<OperationKind> runningOperationKind() const;
[[nodiscard]] Status releaseProbe();     // suspend RTT + close the J-Link connection, keep registry slot + lock
[[nodiscard]] Status reacquireProbe();   // reopen + resume; probe.in_use_by_other_process when taken
[[nodiscard]] Status setRttAddress(std::optional<std::uint32_t> address);   // sticky; used at the next attach
```

- `releaseProbe` keeps the `ProbeLock`: rttt's use case is an external `JLinkExe`/`west flash`, and the lock only
  binds HARDWARIO programs. This is documented in the tool description. The worker implementation reuses the
  existing private close/reopen helpers of the nRF Util path (`SessionWorker::releaseProbe`/`reopenProbe` in
  `session_worker.cpp`); the new worker handlers get distinct names (`detachProbe`/`reattachProbe`) so the two uses
  are not confused.
- `SessionOptions::rttAddress` (`std::optional<std::uint32_t>`) plus `setRttAddress` (sticky) passes the control
  block address to `JLINK_RTTERMINAL_Control(START)` via the existing `RttStart::configBlockAddress`
  (`jlink_abi.h`). `IJLinkApi::rttStart()` becomes `rttStart(std::optional<std::uint32_t> blockAddress)`;
  `JLinkLibrary` passes a `RttStart` struct instead of `nullptr` when set; `RttChannel` and the worker thread it
  through.
- `writeFlash`: reset + halt, then `IProgrammer::program` + `verify` with
  `firmware::FirmwareImage::fromSegments({{address, data}})` (the loader keeps untouched bytes of each sector),
  reset, RTT re-attach. A range outside `flash` (and `uicr` without `allowUicr`) → existing `firmware.out_of_range`;
  data touching the PIB → `firmware.uicr_in_image` unless allowed (M2 rule).
- `writeMemory`: only `ram` and `peripherals` ranges (`target.address_out_of_range` otherwise); units little-endian;
  size a multiple of the width. Width 8 uses `JLINKARM_WriteMem` (already declared; `IJLinkApi::writeMemory` is
  documented today as the flash-loader buffer — its doc changes to "direct write outside a download"), widths 16/32
  use `JLINKARM_WriteU16`/`WriteU32` per unit.
- `readMemory` width: 8 and 16 → existing `ReadMemEx` with access width 1/2 (pylink 2.0.2); 32 → existing
  `JLINKARM_ReadMemU32` (with count). `IProgrammer::readMemory` gains the width parameter.
- **JLinkARM declarations** (verified against pylink 2.0.2 in `../refs/pylink_square-2.0.2`). Already declared and
  used: `JLINKARM_Halt`, `JLINKARM_Go`, `JLINKARM_WriteMem`, `JLINKARM_ReadMemEx`, `JLINKARM_ReadMemU32`. New, resolved
  **optionally** (ADR-0151; missing → `dependency.jlink_function_missing` from the operation that needs it):
  `JLINKARM_IsHalted`, `JLINKARM_GetId`, `JLINKARM_WriteU16`, `JLINKARM_WriteU32`,
  `JLINKARM_GetRegisterList`, `JLINKARM_GetRegisterName`, `JLINKARM_ReadRegs`, `JLINK_GetMemZones`.
  `IJLinkApi`, `MockJLinkApi` (`libs/hw/jlink/tests`) and `ScriptedApi` (`libs/device/tests`) gain the methods.
- **Fake J-Link** (`tests/fake_jlink/fake_jlink.cpp`, `HIO_FAKE_JLINK_SCENARIO`): halted state (`Halt`/`Go`/`IsHalted`,
  reset with halt), a RAM/peripheral memory model (`chip.ram[{address, size}]`, writable via `WriteMem` outside a
  download and `WriteU16/U32`), `chip.registers[{name, value}]`, `chip.zones[]`, `chip.coreId`, the RTT START
  block address recorded and checked against `rtt.blockAddress` (scenario keys camelCase as today), and a scenario
  switch to omit the optional exports (ADR-0151 error path).

### Profile additions

- `TargetProfile::ram` (`firmware::AddressRange`, half-open `[start, end)`) and `TargetProfile::peripherals`
  (`std::span<const firmware::AddressRange>`), with default initializers so the designated initializers in
  `profiles/chester.cpp` keep compiling: CHESTER app (nRF52840) RAM `[0x20000000, 0x20040000)`, peripherals
  `[0x40000000, 0x60000000)` and PPB `[0xE0000000, 0xE0100000)`; lte (nRF9160) RAM `[0x20000000, 0x20040000)`,
  peripherals `[0x40000000, 0x60000000)` + PPB. Values are checked against the Nordic product specifications in
  phase B.
- `TargetProfile::shellPrompt` (`std::string_view`, default empty): the RTT shell prompt, empty until confirmed on
  hardware (open question 25). It is separate from `BleProfile::shellPrompt` (`"bt_nus:~$ "`, the NUS shell) because
  the two shells are different transports with possibly different prompts.
- **RTT prompt detection:** `isPrompt()` moves from the BLE-private `ble_session_internal.h` to the public
  `line_assembler.h` (next to `stripAnsiCsi`/`applyCarriageReturns`); the J-Link worker checks the shell
  assembler's partial line after each RTT read (ADR-0082 rule) and posts a new `SessionEvent` alternative
  `PromptSeen`; `DeviceSession` emits the existing `ConsoleSession::promptSeen`. The `promptSeen` doc changes from
  "BLE only" to "when the profile has a prompt for this transport".

### `ConsoleLog` additions (ADR-0155) and console file directory (ADR-0156)

- `ConsoleLog::total(Channel)` and `ConsoleLog::firstSeq(Channel)`; no behaviour change for M1–M5 users.
- `SessionOptions::consoleFileDir` / `BleSessionOptions::consoleFileDir` (`std::optional<std::filesystem::path>`),
  `ConsoleFileConfig` gets the matching field; an explicit `consoleFile` wins; the generated file name is the same as
  in the default directory.

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

- Stdin: the existing `StdinLineReader` (`app/cli/src/terminal.h`, owned `std::jthread`, 50 ms poll,
  `CancelSynchronousIo` on Windows) gets an options struct (ADR-0154). `hio mcp` uses: max line 4 MiB, queue 64
  lines (blocking → backpressure), `DropAndReport` for longer lines (→ JSON-RPC parse error, rest of the line
  dropped), and a notify callback that posts a queued wake-up to the owner thread. EOF → `stdinClosed`. The console
  keeps 4096 bytes / 256 lines / `Truncate` / no callback. `StdioTransport` (in `hio_mcp`) takes lines through a
  small `ILineSource` interface so the library does not depend on the CLI reader.
- stdout: one JSON message + `\n` per write, flushed, owner thread only. Nothing else is ever written to stdout
  (logging goes to the log file and stderr); the command returns `streamed=true`.
- `mcp_command.cpp`: builds `CliDeviceProvider`, `FlagConsentBroker`, the registry, `McpServer` and the
  transport(s), then runs `QCoreApplication::exec()` (ADR-0097; the first CLI command that does) until stdin EOF,
  shutdown or a stop request; no nested loops. Ctrl+C/SIGTERM: `SignalGuard` only sets `context.stop`
  (from its watcher thread); a `std::stop_callback` posts a queued call to the owner thread → `server.stop()` →
  close sessions (≤ 5 s) → leave the loop → `cancelled` → exit 130.

### Error codes added (texts translatable, ADR-0027; the CLI renders English)

Default texts contain no placeholders and every row has a message, cause and suggestion (rules and checks in
`libs/core/src/error.cpp` and `error_test.cpp`). Values go into `detail`, or the call site builds its own text with
`QCoreApplication::translate(...).arg()` via `fail(code, message, cause, suggestion, detail)`; "call site" below
shows those texts. Adding a code = enum value, `errorTable` row, `isEnumerator` case, `defaultTexts` row
(`QT_TRANSLATE_NOOP3("hio::core", …)`), `error_test.cpp` row, `cs` translation.

| Code | Category | Message / cause / suggestion (table default) | Call site / detail |
|---|---|---|---|
| `mcp.port_in_use` | Io | "The MCP port is already in use." / "Another HARDWARIO Monitor, `hio mcp --http` or another MCP server (for example rttt) is listening on it." / "Stop the other server or choose another port with --port." | detail: port |
| `mcp.listen_failed` | Io | "The MCP server could not start." / "The operating system refused to open the listening socket on 127.0.0.1." / "Choose another port with --port." | detail: port + OS error text |
| `mcp.token_file` | Io | "The MCP token file cannot be read or created." / "The file or its folder has the wrong permissions, or the file does not contain a valid token." / "Check the file permissions, or delete the file to create a new token." | detail: path |
| `mcp.tool_not_allowed` | Usage | "This MCP tool is not allowed by the server." / "Destructive tools must be allowed when the server starts." / "Start the server with --allow and the tool's category, or with --allow all." | call site: "The tool %1 is not allowed by this server." / "Start the server with --allow %2 (or --allow all)." (GUI variant in 0014) |
| `mcp.consent_denied` | Cancelled | "The user did not allow this action." / "The user declined the permission request in HARDWARIO Monitor." / "Ask the user to allow it in HARDWARIO Monitor." | GUI only (0014); detail: tool |
| `mcp.consent_timeout` | Timeout | "Nobody answered the permission request." / "The permission request in HARDWARIO Monitor was not answered within 2 minutes." / "Ask the user to watch HARDWARIO Monitor and try again." | GUI only (0014); detail: tool |
| `mcp.device_ambiguous` | Usage | "Several devices are connected and the tool does not know which one to use." / "The device argument was left out while more than one device is open." / "Pass the device argument; list_devices shows the ids." | detail: open ids |
| `mcp.device_not_open` | Usage | "The device is not connected." / "It was never connected, it was disconnected, or its tab was closed." / "Call connect first." | detail: id |
| `mcp.capability_missing` | Usage | "This tool needs a J-Link connection." / "The device is connected over Bluetooth, which provides only the console." / "Connect the device with a J-Link probe." | detail: id |
| `mcp.too_many_requests` | Internal | "Too many MCP requests are running." / "The server runs at most 32 tool calls at the same time." / "Wait for running calls to finish and try again." | — |
| `target.not_halted` | Target | "The CPU is running." / "Registers can be read only while the core is halted." / "Halt the core first." | — |
| `target.address_out_of_range` | Target | "The address range is not allowed for this operation." / "Memory writes are allowed only in the RAM and peripheral ranges of the target." / "Use an address inside the RAM or peripheral ranges of the target." | detail: requested range + allowed ranges |
| `target.memory_access_failed` | Target | "Reading or writing target memory failed." / "The address is not mapped, or the core is in a fault state." / "Check the address and try again after a reset." | detail: address, J-Link return code |
| `io.file_exists` | Io | "The output file already exists." / "Existing files are never overwritten by this command." / "Choose a new file name or delete the file first." | detail: path |
| `dependency.jlink_function_missing` | Dependency | "The installed J-Link software does not provide a function this operation needs." / "The J-Link library is older than, or different from, the versions HARDWARIO Monitor was tested with." / "Install the latest J-Link Software and Documentation Pack." | detail: function name (ADR-0151) |

`writeFlash` range errors reuse `firmware.out_of_range`. Exit codes follow the existing category map
(`app/cli/src/exit_codes.cpp`): Internal 1, Usage 2, Io 3, Dependency 4, Probe 5, Target 6, Firmware 7, Network 8,
Timeout 9, Cancelled 130.

Protocol-level HTTP refusals (401/403/421/413/…) are not `hio::Error`s. They are logged in `hio.mcp`, with the
token never logged.

### Threading summary

Owner thread: `McpServer`, dialects, transports, `HttpServer`/connections, all `ToolCall`s, provider, consent
broker, `ConsoleLog` reads. Session worker threads: unchanged, plus the new operations. The CLI stdin reader is
one `std::jthread`, with only the bounded line queue and a queued wake-up crossing threads (ADR-0097, ADR-0154).
`SignalGuard`'s watcher thread only requests the stop token. Phase D: the `doctor` tool runs `CheckRunner` on an
owned `std::jthread`; the BLE host thread is unchanged (ADR-0141).

### Bounds

In-flight calls 32 · HTTP connections 16, headers 16 KiB, body 4 MiB, header timeout 10 s, body 30 s, idle 60 s,
per-connection send queue 1 MiB · legacy sessions 16, idle 30 min · listen streams 16 · subscriptions 64,
updates ≤ 1/s · progress ≤ 4/s · stdio line 4 MiB, stdin queue 64 lines · auth failures 10/60 s → 10 s lockout ·
`send_command` timeout 0.1–120 s, output 2000 lines / 256 KiB · `read_log`/`read_terminal` ≤ 5000 lines / 1 MiB ·
`wait_for_*` ≤ 600 s · regex ≤ 512 chars · `read_memory` 64 KiB inline, 16 MiB to file · `write_memory` 64 KiB ·
`write_flash` 1 MiB · image files 64 MiB · operation stall 60 s · sessions 8 (registry, shared with BLE) · activity
entries 1000 (sink) · registers 128, zones 32 · fuzz smoke 60 s per target.

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
| **Sync 2026-10-20:** library split (tools need `hio::device`) | two libs / one lib | two: `hio_mcp` (protocol, Core+Network) + `hio_mcp_tools` | ADR-0150 |
| **Sync:** English rendering of translated backend errors (ADR-0103) | defer to 0014 / extend `hio::Error` now / amend ADR-0103 | defer to 0014 (the CLI is English already) | — |
| **Sync:** new JLinkARM symbols | optional / mandatory | optional; `dependency.jlink_function_missing` | ADR-0151 |
| **Sync:** fuzzing | `linux-fuzz` preset / none | `linux-fuzz` preset, HTTP + JSON-RPC fuzzers, 60 s CI smoke | ADR-0152 |
| **Sync:** confirmation for `mem write` / `flash-write` | both / flash-write only / none | both ask (amends ADR-0038) | ADR-0153 |
| **Sync:** stdin reader | extend `StdinLineReader` / new class | extend (options struct) | ADR-0154 |
| **Sync:** `ConsoleLog` totals for `status`/`dropped_before` | extend API / drop fields | extend: `total`, `firstSeq` | ADR-0155 |
| **Sync:** `--console-file-dir` | default dir only / new option | new option, `consoleFileDir` in session options | ADR-0156 |
| Minor defaults: all bounds and timeouts above; tool names (rttt names kept, `jlink_close/open` → `release_probe/reacquire_probe`); `read_memory` `to_file` never overwrites; ConsoleLog not cleared after flash (cursors instead); BLE gets `mcp.capability_missing` for J-Link tools; `releaseProbe` keeps the lock; error codes, categories and texts above; CHESTER RAM/peripheral ranges (checked in phase B); new CLI commands `status/halt/go/regs/zones/mem/flash-write`; `lcMcp` = `hio.mcp`; JSON for 250 ms calls, SSE otherwise. **Added in the sync:** `OperationResult` per-kind fields instead of a variant; `runningOperationKind()` for the busy detail; `flash` result without `events[]`; `writeFlash` range errors reuse `firmware.out_of_range`; `TargetProfile::shellPrompt` separate from `BleProfile::shellPrompt`; `isPrompt` public in `line_assembler.h` + `SessionEvent::PromptSeen`; `IJLinkApi::rttStart(optional address)`; worker handlers `detachProbe`/`reattachProbe`; shared CLI `operation_runner`; `Limits` struct instead of a fake clock; CLI e2e tests in `app/cli/tests`; `HIO_MCP_TOKEN` production env var | accept / change | accepted 2026-10-20 | — |

## Decisions during implementation (2026-10-20, by Matej Strnad)

Answers to the implementation agent's questions; they override the text above where it differs.

| # | Question | Decision | ADR |
|---|---|---|---|
| Q1 | RTT during light target operations | stays attached on the console target (rttt parity); M2 pattern on other targets | ADR-0157 |
| Q2 | 16-bit reads | `ReadMemEx` with access width 2 (pylink); `JLINKARM_ReadMemU16` not used | ADR-0151 note |
| Q3 | `cacheScope` (`"client"` is invalid) | `"private"` everywhere; ttlMs 3 600 000 discover/tools/prompts/templates, 5000 resources/list, 0 resources/read | — |
| Q4 | `hio mcp` event loop (plan revision contradicted ADR-0097) | `QCoreApplication::exec()` per ADR-0097 | ADR-0097 |
| — | Where the target-operation logic lives | `IProgrammer`/`JLinkProgrammer`; worker free functions in `session_worker_target.cpp` | ADR-0158 |
| — | `IDeviceProvider::refresh` blocking on the owner thread | asynchronous `refresh(std::function<void(Status)> done)`, `done` once on the owner thread; `list()` returns the cached list; follows ADR-0162 (R11) | ADR-0162 |

## Open findings (independent review of phases A–C, 2026-10-20)

Verification before the review (re-run by the planning session): linux-debug 1206/1206, linux-tsan 1192/1192,
clang-format, check-includes, check-lfs, check_translations clean; the agent also reports linux-asan 1206/1206,
linux-tidy 0 findings, both fuzzers 60 s without findings.

| # | Sev. | Finding | Suggested fix | Status (2026-10-23) |
|---|---|---|---|---|
| R9 | **high** | `write_memory` / `mem write` may write the NVMC (nRF52840 `0x4001E000`, nRF9160 `0x50039000`/`0x40039000`), which lies inside the writable peripheral range: ERASEALL / ERASEUICR / ERASEPAGE wipe flash, UICR and the PIB with only the `memory-write` consent (breaks ADR-0036, ADR-0101) | leave the NVMC block out of the writable ranges per target (`TargetProfile::writeDenied`), tests | fixed |
| R1 | medium | HTTP: a browser page can keep the auth lockout active (no-Origin no-cors GETs count as auth failures) → every real client gets 429 | reject `Sec-Fetch-Site: cross-site\|same-site` with 403 before the token step; count only requests with an `Authorization` header as failures | fixed |
| R2 | medium | `--token-file` hardens the parent directory (chmod 0700 `$HOME`, replaces the Windows DACL of the home dir); a bare relative name always fails after creating the file | harden only the default directory (or one the store created); for user paths check the file (+ refuse group/other-writable dir); make the path absolute | fixed |
| R3 | medium | Windows: token written to the temp file before the owner-only DACL is applied | create the temp file with the owner-only security descriptor | fixed (Windows code reviewed, not compiled here) |
| R10 | medium | `read_log` with `pattern` scans up to 100 000 lines × regex synchronously on the owner thread (GUI freeze; ADR-0097); `wait_for_state` timeout drains the backlog synchronously | cap lines scanned per call + continuation cursor (or `ScanScheduler`), cap line length for matching | fixed |
| R11 | medium | blocking work on the owner thread: Intel HEX parse (≤ 64 MiB) in `flash`, LTE zip classification, `enumerateProbes` in `CliDeviceProvider::refresh()` | run them on a worker thread and post the result back | fixed |
| R12 | low/med | translation context mismatch (`translate("hio::mcp")` vs extracted `hio::mcp::tools::detail`); MCP error texts are marked translatable against ADR-0103 | MCP error texts plain English; markers/busy texts with a correct context (used by 0014) | fixed |
| R4 | low | refused HTTP connections have no timeout in `Closing` → slots can be exhausted | 5 s abort timer | fixed |
| R5 | low | duplicate in-flight request ids accepted | `-32600` "duplicate id" | fixed |
| R6 | low | modern-dialect consent/activity keyed by the self-declared `clientInfo.name` | decide in 0014 (CLI unaffected) | moved to 0014 |
| R7 | low | invalid JSON-RPC notification gets an error line on stdio; ids > 2^53 rejected | note for the conformance run (E1) | note added (E1) |
| R8 | low | `startStream()` before `watchChannel()` → a close during the SSE header write is missed | swap the order | fixed |
| R13 | low | chained `.arg(path).arg(n)` corrupts texts when the path contains `%1` | multi-argument `.arg(a, b)` | fixed |
| R14 | low | cancel racing with completion reports `cancelled` for an operation that succeeded | report the success | fixed |
| R15 | low | `read_memory to_file`: creates missing parent dirs silently; on Windows a "bare name" like `C:foo` escapes the cache folder | no directory creation; reject drive-relative / stream names | fixed |
| R16 | uncertain | the 60 s stall limit may cancel an nRF Util modem update in a silent phase | check in E3 HIL | open (HIL E3) |
| R17 | low (0014) | `connect`: with an async provider a session opened after the call ended is never closed | close it in the callback | moved to 0014 |
| R18 | low | 16-bit read accepts a partial read when the count equals size/2 | pin the unit after E3, require the exact count | open (HIL E3) |
| R19 | low | a released probe is reopened silently by `reconnect()`/`resume()` (→ `probe.open_failed`, not `in_use_by_other_process`); `reacquireProbe` without release re-connects | `m_released` state; reconnect/resume go through `reattachProbe`; reacquire without release = no-op | fixed |
| R20 | low | light op on a Disconnected/Stopped console session leaves the state stale | restore/close the probe after the op | fixed |

Choices made without a plan rule that the review flagged (S) and the implementation agent's open questions (Q):

| # | Topic | Current behaviour | Recommendation | Status (2026-10-23) |
|---|---|---|---|---|
| S1 | reads restricted to flash/UICR/RAM/peripherals (FICR, ROM table unreadable; rttt reads anywhere) | restricted | reads unrestricted (fault → `target.memory_access_failed`) | fixed |
| S2 | `write_flash` consent | always `flash`, and `uicr` even when `allow_uicr=false` | same rule as `flash`: `flash` for bytes outside UICR, `uicr` only with `allow_uicr` and UICR bytes | fixed |
| S3 | `erase scope=all` / `recover` wipe the PIB with only `erase` | as plan table | require `erase` + `uicr` (ADR-0101: `uicr` protects the PIB) | fixed |
| S4 | stdout write failure in `hio mcp` | exit 0 | existing `outputClosedError` path (non-zero) | fixed |
| S5 | stdin EOF cancels in-flight calls | per ADR-0097 | keep; document in `--help` | fixed (documented in `--help`) |
| S6 | `targetStatus` when `JLINKARM_GetId` is missing | whole op fails | return `halted` without core id | fixed |
| S7 | `halt` on the console target does not mark the core halted for auto-reconnect | — | set the halted flag like `reset --halt` | fixed |
| S8 | CLI asks for confirmation before the range check | — | range check first | fixed |
| S9 | `ReadMemEx` failure → `target.memory_access_failed` (was `target.connect_failed`) also for `pib/uicr read`, UICR merge, PIB guard | changed | accept (more precise, same exit code) | accepted |
| S10 | `memoryZones` truncation not logged | — | log like registers | fixed |
| Q1 | legacy `initialize` with an unknown version | answered with 2025-11-25 as the spec requires (ADR-0095 says error) | follow the spec; new ADR amending ADR-0095 | kept, tested (golden) |
| Q2 | graceful end of `subscriptions/listen` | final result | accept | accepted |
| Q3 | new `ToolSpec::validate` hook in `hio_mcp` | added | accept | accepted |
| Q4 | `flash verify=false` | rejected with -32602 | drop the argument (M2 always verifies) | fixed |
| Q5 | `connect` `auto_reconnect` default | provider default (`--auto-reconnect`) | accept | accepted |
| Q6 | stdin worst case 64 × 4 MiB = 256 MiB | — | add a 16 MiB total byte budget to the queue | fixed |
| Q7 | `dropped_before` is an upper bound | — | accept, documented | accepted |
| Q8 | no end markers for halt/go/write_memory | — | accept | accepted |
| Q9 | `FlagConsentBroker` texts not extracted by lupdate | CLI is English | accept; revisit in 0014 | accepted (0014 note) |
| Q10 | fake `chip.halted` key missing (CLI `regs` success path untested e2e) | — | add the key + test | fixed |
| Q11 | 32-bit Windows stdcall of WriteMem etc. | unchanged | accept (Windows builds are x64) | accepted |
| Q12 | 16-bit ReadMemEx count unit | both accepted | HIL E3 (with R18) | open (HIL E3) |
| — | `Accept: */*` accepted; modern `notifications/cancelled` over HTTP scoped to the connection; modern method-not-found → HTTP 404 | as implemented | accept, record in the plan | accepted |

**Status 2026-10-23:** all rows fixed except those moved to 0014 (R6, R17) and those left for HIL E3 (R16, R18,
Q12). Verified after the fixes: linux-debug 1266/1266, linux-asan 1266/1266, linux-tsan 1252/1252 (13 HIL tests
skipped in each), linux-tidy 0 findings, clang-format, check-includes, check-lfs, all_qmllint, check_translations
(0 unfinished), both fuzzers 60 s without findings.

**User decisions on the findings (2026-10-20):** R9 → NVMC excluded from writes (ADR-0160); S1 → reads unrestricted
(ADR-0160); S3 → `erase scope=all`/`recover` need `erase` + `uicr` (ADR-0161); Q1 → follow the spec (ADR-0159);
R11 → worker threads (ADR-0162); Q6 → byte budget, settings key `mcp/stdioBufferMiB` default 16 MiB, 4–256
(ADR-0163); Q4 → `verify` argument dropped; all other rows as recommended (R6/R17 go to 0014, R16/R18/Q12 to HIL
E3).

## Open findings (focused review of the fix round, 2026-10-23)

Re-verification by the planning session: linux-tsan 1252/1252; linux-debug 1263/1266 at load ≈ 9 (the three
failures pass 3/3 when re-run alone; one of them shows V1); clang-format, check-includes, check-lfs clean. The
focused review confirms 22 fixed rows (R1–R5, R8–R15, R19, R20, S2–S4, S8, Q4, Q6).

| # | Sev. | Finding | Suggested fix | Status (2026-10-23) |
|---|---|---|---|---|
| F1 | medium | `memory-write` still allows code execution: the PPB (`0xE0000000–0xE0100000`) and RAM are writable, so VTOR + NVIC STIR, DCRSR/DCRDR (PC) or an overwritten stack + `go` can run code that erases the PIB. ADR-0160's "the PIB can only be lost through erase/recover" does not hold | decision needed (amends ADR-0160) | fixed |
| F2 | low-med | offloaded jobs ignore their `stop_token` (`readIntelHexFile`, `classifyLteInput`, `enumerateProbes`); `McpServer::stop()` → `Offloader::stopAll()` joins them on the owner thread (GUI freeze in 0014) | pass the token into the HEX record loop and zip extraction; enumeration is bounded (≤ 2 s) | fixed |
| F3 | low | `CliDeviceProvider` refresh waiters stranded forever if the job is dropped (cancel / `stopAll`) | fail the waiters with `cancelled` when the job handle is dropped | fixed |
| F4 | low | MCP `write_memory`/`write_flash` ask for consent before the range check (S8 not applied to MCP; matters for 0014 dialogs) | range checks in the tool builders before consent | fixed |
| F5 | low, uncertain | `read_memory to_file` accepts Windows reserved names (`NUL`, `CON`, `COM1`), trailing dots/spaces | reject them | fixed |
| V1 | low | first `list_devices` (no `refresh`) right after `hio mcp` starts can return an empty list: the startup enumeration runs on the offloader (R11) | decision needed | fixed |
| — | open point | CLI `checkFlashWrite` duplicates `planImage` flash rules (CLAUDE.md: the CLI contains no logic) | public `device::checkFlashWrite(profile, address, size, allowUicr)` in `profile.h`, used by `planImage`, the CLI and the MCP builder (also fixes F4) | fixed (`device::checkFlashWrite`) |
| — | flaky under load | `OwnerThread.ThirtyTwoWaitsOnAFastStreamDoNotStall`, `BleSessionTest.ExplicitReconnectAlwaysTries`, `BleSessionTest.ReopenRightAfterCloseIsSafe` | note; pass on an idle machine | noted (pass when re-run alone) |

**Status 2026-10-23 (fix round 2):** F1–F5, V1 and the `checkFlashWrite` point fixed. Verified: linux-debug
1274/1274, linux-asan 1274/1274, linux-tsan 1258/1260 in the full run (`BleSessionTest.ExplicitReconnectAlwaysTries`
and `tst_gui.tst_shell` (glib eventfd race in uninstrumented code) pass when re-run alone), linux-tidy 0 findings,
clang-format, check-includes, check-lfs, all_qmllint, check_translations (0 unfinished), both fuzzers 60 s without
findings.

**User decisions on the focused review (2026-10-23):** F1 → SCS + FPB also write-denied, `memory-write` documented as
code execution (ADR-0164); V1 → `list_devices` without `refresh` waits for the first startup scan, then returns
the cache; `checkFlashWrite` → public `device::checkFlashWrite(profile, address, size, allowUicr)` in `profile.h`
used by `planImage`, the CLI and the MCP `write_flash` builder before consent; F2–F5 as suggested.

## Implementation steps

Phase A — protocol core (`hio_mcp`)
- [x] A0 Prerequisite check: M1–M4 and the M5 `ConsoleSession`/`BleSession` code present (they are on `v2/m5-ble`);
  otherwise stop and report
- [x] A1 CMake: `hio_mcp` and `hio_mcp_tools` targets (+ the `hio_mcp` link check), `lcMcp`, all error codes in
  `libs/core` (+ `cs` translations)
- [x] A2 `json_rpc` + `protocol.h` + `schema` (builders, validation) + `Limits` + tests
- [x] A3 `Registry`, `ToolCall`, `McpServer` with both dialects (handshake, discover, list/call/read/get, errors,
  progress, cancellation, subscriptions) + golden-exchange tests per dialect
- [x] A4 `IConsentBroker`, `IActivitySink` (in `hio_mcp`), `IDeviceProvider` (in `hio_mcp_tools`) + mocks

Phase B — backend additions
- [x] B1 JLinkARM declarations (optional resolution, ADR-0151) + `IJLinkApi`/`MockJLinkApi`/`ScriptedApi` methods +
  `rttStart(address)` + pylink cross-check + fake J-Link model (halt state, RAM/peripherals, registers, zones,
  RTT block address, omitted exports)
- [x] B2 `DeviceSession` operations `halt/go/targetStatus/readMemory(width)/writeMemory/readRegisters/
  memoryZones/writeFlash`, `runningOperationKind`, `releaseProbe/reacquireProbe`, `rttAddress`/`setRttAddress` +
  tests (fake + GMock)
- [x] B3 Profile `ram`/`peripherals`/`shellPrompt`, `isPrompt` moved, RTT prompt detection, `SessionEvent::PromptSeen`,
  `promptSeen` on `DeviceSession`; `ConsoleLog::total/firstSeq` (ADR-0155); `consoleFileDir` (ADR-0156) + tests
- [x] B4 CLI `operation_runner` extraction; `chester app|lte status|halt|go|regs|zones|mem read|mem write|flash-write`
  (ADR-0153 confirmation) + e2e tests

Phase C — tools and transports
- [x] C1 Discovery, console, session-control tools (+ `send_command` per ADR-0104 with shrunk `Limits`) + tests
- [x] C2 Target and firmware tools (consent categories incl. run-time `uicr`, markers, busy, stall) + tests
- [x] C3 Resources and the `debug_device` prompt + tests
- [x] C4 `StdioTransport` (`ILineSource`), `StdinLineReader` options (ADR-0154) + tests (TSan)
- [x] C5 `HttpParser`, `HttpServer`, `TokenStore` + tests (limits, check order, SSE, keep-alive, slow client);
  `linux-fuzz` preset, `fuzzer` in `HioSanitizers.cmake`, `http_parser_fuzz` + `json_rpc_fuzz` with seed corpora,
  Linux CI job (60 s each) (ADR-0152)
- [x] C6 `hio mcp [--http]`: `CliDeviceProvider`, `FlagConsentBroker`, stop bridging, `--console-file-dir` + CLI e2e
  tests (both transports, both dialects)

Phase D — tools that depend on M4/M5 code (only after the user says M5 has settled; not registered until then)
- [ ] D1 `diagnose`, `doctor` (check JSON builder moved from `app/cli/src/diag_commands.cpp` into `libs/diag`;
  `CheckRunner` on an owned thread)
- [ ] D2 BLE devices in `list_devices`/`connect`, `ble_scan` (provider-owned `BleHost`, `bleInUse`)

Phase E — verification and docs
- [ ] E1 Conformance: official MCP conformance suite + MCP Inspector against `hio mcp --http` and stdio
  (manual; optional CI job behind `HIO_MCP_CONFORMANCE=1`, Node not required for the normal CI)
- [ ] E2 Interop (manual, by the user): Claude Code (stdio and HTTP), Claude Desktop (stdio), Cursor (HTTP, legacy)
- [ ] E3 HIL (`HIO_HIL=1`, CHESTER; ask the user before every hardware step): flash → wait_for_connection →
  send_command; halt/regs/go; read/write RAM; write_flash into a scratch page and restore; RTT prompt recorded into
  the profile (open question 25)
- [ ] E4 Update CLAUDE.md "Commands" (`hio mcp`, `linux-fuzz`), `0006-cli.md`, `0004-mcp.md`, roadmap

## Tests and verification

- **Protocol (both dialects):** handshake, version negotiation (supported, older legacy, unknown → -32022 with
  list), batch rejected, notifications not answered, ids echoed (string/int), invalid params with pointer, unknown
  tool, `resultType`/`ttlMs` present (modern), header mismatch (-32020), session id required/forbidden,
  legacy session expiry and DELETE, cancellation before/after start, progress throttling, subscription updates
  throttled, list_changed on open/close.
- **HTTP:** every check-order branch with exact status codes; Origin variants (`null`, other port, `localhost`,
  IPv6 literal); Host variants; token wrong/missing/constant-time path; lockout after 10 failures; header 16 KiB+1,
  body 4 MiB+1; chunked request; slowloris header timeout; keep-alive reuse; SSE heartbeat; stream close cancels;
  slow client disconnected at 1 MiB; 17th connection refused; port in use → `mcp.port_in_use`; fuzzing
  (`linux-fuzz`, 60 s per target).
- **Tools (fake J-Link scenarios + mock provider/broker):** each tool's happy path and validation errors;
  `send_command` ends by prompt / silence / timeout, echo filtering, `interleaved`, output caps; `read_log` cursor
  across eviction (`dropped_before`); `wait_for_state` passive and polling, invalid regex; `flash` with path,
  catalog id, both (→ -32602), UICR image without/with `allow_uicr` (consent `uicr` requested), cancel mid-flash,
  stall (shrunk limit); `internal.busy` with the kind; device closed mid-call (`QPointer`); `device` omitted with
  0/1/2 sessions; BLE session (fake BLE backend) → `mcp.capability_missing`; missing optional J-Link export →
  `dependency.jlink_function_missing`.
- **Backend:** new operations against GMock'd `IJLinkApi` (call order, widths, little-endian units, halted check,
  range checks incl. half-open edges, PIB protection, verify failure, RTT block address passed) and fake scenarios;
  `releaseProbe` keeps the lock; RTT `promptSeen` with a profile prompt and never without; `ConsoleLog::total/firstSeq`
  across eviction and `clear`; `consoleFileDir` for J-Link and BLE sessions; M1–M5 suites green unchanged.
- **CLI e2e:** `hio mcp` over stdio with scripted JSON-RPC (both dialects): initialize → list → connect → flash
  (fake) → status; `--allow` missing → `mcp.tool_not_allowed`; `--json` → exit 2; stdin EOF → exit 0, sessions
  closed; SIGINT → 130 without hang; nothing but JSON on stdout (checked line by line); `--console-file-dir`
  missing dir → error before serving. `hio mcp --http` with requests from a Qt test client. New target commands,
  human and `--json`; `mem write`/`flash-write` without TTY and without `--yes` → `usage.confirmation_required`;
  `mem read -o` existing file → `io.file_exists`.
- **Owner-thread stalls:** with 32 in-flight `wait_for_state` calls and 10 000 lines/s from the fake, no owner-thread
  task takes more than 20 ms (measured in the test).
- **Sanitizers / static analysis / translations:** linux-debug, linux-asan (ASan+UBSan), linux-tsan (stdin reader,
  sessions) ctest; linux-tidy 0 findings; clang-format-21; `tools/check-includes.sh`; `tools/check-lfs.sh`;
  `all_qmllint`; `update_translations` + `check_translations`; `linux-fuzz` smoke.

## Risks and open points

- **Spec churn:** 2026-07-28 is new and clients are still migrating. Both dialects are isolated adapters. Interop is
  checked manually before Done (E2); differences found there need a plan update and the user's approval.
- **Client timeouts:** long flashes (LTE modem update, minutes) can exceed some clients' tool timeouts. Progress
  notifications help where clients reset their timeout on progress; the tool descriptions state typical durations.
- **CHESTER RTT prompt** is unknown (open question 25). Until it is confirmed, `send_command` uses the silence rule.
- **New JLinkARM functions** need the ABI cross-check (pylink 2.0.2, ADR-0028) and HIL. `JLINK_GetMemZones` returns
  nothing for some devices; the tool then returns an empty list. A missing export only disables its operation
  (ADR-0151).
- **Security of a localhost server:** other local users can reach 127.0.0.1. The token protects that; the token
  file permissions are verified on every platform in E2.
- **Own HTTP parser** is new attack surface: fuzzing plus strict limits, and no features beyond what MCP needs.
- **Parallel M5 work:** 0011 is still being fixed on `v2/m5-ble` (round-2 findings N1–N6, HIL). Changes in
  `libs/device` (`ConsoleLog`, `line_assembler.h`, `BleSessionOptions`, `session_worker.cpp`) must be merged with
  the later `v2/m5-ble` commits before M6 is merged; conflicts are expected to be small.

## Sync with the code (2026-10-20)

Differences between the 2026-10-16 text and the code on `v2/m5-ble` 7dc190a, and how the text above resolves them.

| # | Plan text (2026-10-16) | Code | Resolution |
|---|---|---|---|
| 1 | `libs/mcp` is Core + Network only, yet holds tools with `DeviceRef` | tools need `hio::device` (J-Link, nRF Util, Qt Bluetooth) and `hio::diag` | two libraries (ADR-0150) |
| 2 | ADR-0004 check = "Core and Network only" | `hio_add_backend_library` enforces a GUI-module denylist | denylist + extra `hio_mcp` link check |
| 3 | error table uses `%1…%3` in default texts | rule: no placeholders (`error.cpp:155`, `error_test.cpp`) | table rewritten; values in `detail` or call-site texts |
| 4 | several rows have cause "—"; listen_failed cause "%2" | `defaultTextsComplete()` requires message, cause and suggestion | every row has all three; OS error in `detail` |
| 5 | `tool_not_allowed` with two suggestions | one default per code | GUI variant is a call-site text (0014) |
| 6 | "MCP renders English" | errors are translated at creation; no English re-render | CLI is English; GUI deferred to 0014 |
| 7 | tool-error example without cause/detail/wizard | `hio::toJson(Error)` emits them | example fixed |
| 8 | `hio::device::LockOwner` | `ProbeLockOwner` (`probe_lock.h`), BLE `BleUse` | renamed |
| 9 | `internal.busy` with the running kind | `runningOperation()` returns the id only | new `runningOperationKind()` |
| 10 | `OperationResult` gains `std::variant` payloads | per-kind fields (`data`, `checks`) | per-kind fields |
| 11 | `readMemory(…, width)` new | `readMemory` exists without width; `IProgrammer::readMemory` too | width added to both |
| 12 | B1 adds Halt/Go/WriteMem/ReadMemU32 | already declared, resolved and in `IJLinkApi` | only the 9 genuinely new functions |
| 13 | new symbols as usual | every symbol mandatory at load | optional (ADR-0151) |
| 14 | `writeMemory` via WriteMem | `IJLinkApi::writeMemory` documented as flash-loader buffer; fake rejects it outside a download | doc change + fake RAM model |
| 15 | "fake scenarios gain the matching ops" | fake has no halt state, no RAM, no registers/zones; `ScriptedApi`/`MockJLinkApi` implement the interface | fake model described; mocks listed |
| 16 | `rttAddress` passed to RTT START | `rttStart()` passes `nullptr`; `RttStart::configBlockAddress` unused | `rttStart(optional address)` threaded through |
| 17 | `releaseProbe` new | private worker `releaseProbe()` exists (nRF Util) | public API kept; worker handlers `detachProbe`/`reattachProbe` reuse it |
| 18 | `src/worker_target_ops.cpp` | worker is `session_worker.cpp` (single switch) | `session_worker_target.cpp`, free functions (ADR-0158) |
| 19 | `TargetProfile::shellPrompt` | `shellPrompt` exists on `BleProfile` only | separate RTT field, both documented |
| 20 | prompt detection "in the RTT line path" | `isPrompt` private to BLE; no prompt `SessionEvent` | `isPrompt` public; `SessionEvent::PromptSeen` |
| 21 | ranges written inclusive | `AddressRange` is half-open `uint64_t`; profile fields lack defaults | half-open values; defaults for new fields |
| 22 | default `target` = "profile's console target" | no such profile field; session `consoleTargetId()` / `BleProfile::consoleTargetId`; lte has no RTT | default = session `consoleTargetId()` |
| 23 | `status` totals, `read_log` `dropped_before` | `ConsoleLog` has no totals or first seq | ADR-0155 |
| 24 | `--console-file-dir` | does not exist; only `consoleFile` | ADR-0156 |
| 25 | `--auto-reconnect`, `--jlink-speed` as if global | per command / group | options of `hio mcp` itself |
| 26 | new `StdinReader` | `StdinLineReader` exists (50 ms, 256 lines, 4096 bytes, polled) | extended (ADR-0154) |
| 27 | "SignalGuard → server.stop()" | `SignalGuard` only sets the stop token from a watcher thread | `stop_callback` + queued call |
| 28 | `QCoreApplication::exec()` | no command runs `exec()`; `runEventsUntil` pattern | `exec()` as ADR-0097 (the 2026-10-20 revision first said `runEventsUntil`; corrected by the user) |
| 29 | stdin EOF → exit 0 | `main` prints an envelope unless `streamed` | `streamed=true` |
| 30 | `target_commands.cpp` opens sessions | `runOperation` is file-local in `flash_commands.cpp` | shared `operation_runner` |
| 31 | `flash-write` "asks like erase" | plain `erase` never asks (ADR-0038) | ADR-0153 |
| 32 | `-o FILE` must not exist | `uicr read -o` overwrites | new commands never overwrite; `uicr read` unchanged |
| 33 | fuzz target "ASan preset" | no fuzzing support; AppleClang lacks libFuzzer | `linux-fuzz` (ADR-0152) |
| 34 | "with fake clock" | no owner-thread clock; `hio::jlink::Clock` is a blocking worker clock | `Limits` struct shrunk in tests |
| 35 | `doctor` tool via `CheckRunner` | `CheckRunner::run` is synchronous (≤ 16 × 15 s); JSON builder CLI-private | owned thread; builder moved to `libs/diag` (phase D) |
| 36 | `diagnose` → `CheckResult` | `OperationResult::checks` = `DiagnoseStep` (no title) | titles from `diagnoseSteps(target)` |
| 37 | `ble_scan` `in_use_by`, timeout in s | `BleDeviceInfo` has no in-use field; `ScanOptions::duration` in ms; needs `BleHost&` | `bleInUse`; ms conversion; provider-owned `BleHost` |
| 38 | `flash` result `events[]` | no events list in `OperationResult` or CLI JSON | dropped |
| 39 | `target.address_out_of_range` for `writeFlash` | `firmware.out_of_range` exists | reused for flash ranges |
| 40 | Fake J-Link under `libs/hw/jlink/` | fake is `tests/fake_jlink/fake_jlink.cpp` | path fixed |
| 41 | `tests/mcp/` top-level | e2e tests live in `app/cli/tests` (`cli_process.h`) | tests per library + `app/cli/tests` |
| 42 | `waitForFirstAttempt` semantics | helper runs a nested `QEventLoop` | re-implemented on `connectionChanged` (already intended) |
| 43 | `read_pib`/`read_uicr` for any target | CLI `pib/uicr read` exist for app only | app target only |

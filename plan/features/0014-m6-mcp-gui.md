# 0014 — M6 MCP in the GUI: server lifecycle, tabs as devices, consent, activity, settings

- **Status:** Draft
- **Milestone:** M6 (plan 2 of 2, ADR-0094)
- **Author:** Matej Strnad (with Claude), 2026-10-16
- **Related ADRs:** ADR-0003, ADR-0008, ADR-0025, ADR-0027, ADR-0038, ADR-0043, ADR-0047, ADR-0058, ADR-0059,
  ADR-0063, ADR-0094–ADR-0105, ADR-0107, ADR-0108
- **Depends on:** [0013](0013-m6-mcp-core-cli.md) (`libs/mcp`, tools, `TokenStore`), [0004](0004-m3-gui-shell.md)
  (shell, tabs, rail, busy API), [0005](0005-m3-console.md) (console page, markers),
  [0006](0006-m3-flash-settings.md) (Settings page with contributed sections). BLE tabs as MCP devices need
  [0012](0012-m5-ble-gui-pairing.md); without it only J-Link tabs are listed.
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Pending amendment from M7 ([0015](0015-m7-packaging-release.md) decision table):** ADR-0108 asked M7 to reconsider
> native notifications for consent prompts; the user decided on 2026-10-19 to keep the own always-on-top window, so nothing changes here.

## Context

ADR-0008: the GUI hosts MCP over Streamable HTTP on 127.0.0.1, opt-in, and the AI works on **the same live
session the user sees**. 0013 delivers the server, the tools and the seams `IDeviceProvider`, `IConsentBroker`
and `IActivitySink`. This plan connects them to the GUI: the open tabs are the devices, consent comes from a prompt window,
and the activity is visible in the console and on the MCP section of Settings. The old app had no MCP; rttt's
`--mcp` has no UI at all.

## Goals / Non-goals

**Goals**
1. `McpController` (app/gui): starts and stops `McpServer` + `HttpServer` from the settings, owns `TokenStore`, and
   reports the state (off / listening / N clients / error).
2. `GuiDeviceProvider`: tabs ↔ device ids, `connect` opens a tab after consent, `disconnect` only for AI-opened
   tabs, busy state, markers and the console hint (ADR-0098, ADR-0105).
3. `GuiConsentBroker` + `ConsentWindow` (its own always-on-top window, 1Password-style, ADR-0108): categories, Ask / Always allow, per-client remembering, queue, timeout
   (ADR-0101).
4. Settings → **AI assistants (MCP)** section: enable, port, token, client snippets, consent rules, activity list.
5. Rail indicator "MCP" with state, opening the section.
6. EN + CS texts, accessibility, mockups, Qt Quick tests.

**Non-goals**
- Protocol, tools or backend changes (0013). If the GUI needs one, stop and amend 0013.
- Remote access, TLS, OAuth (ADR-0102).
- An MCP client inside the app (the app does not talk to an LLM).

## User experience

### Settings → AI assistants (MCP)

```
┌ AI assistants (MCP) ─────────────────────────────────────────────────────────┐
│ [■ ] Allow AI assistants to use HARDWARIO Monitor          ● Listening        │
│      AI tools such as Claude Code can read the console, send commands and,   │
│      with your permission, flash devices.                                    │
│ Address   http://127.0.0.1:[47800]/mcp                            [Copy]      │
│ Token     ••••••••••••••••••••••••    [Show] [Copy] [Regenerate…]             │
│ Connect a client                                                             │
│   Claude Code (HTTP)    claude mcp add --transport http hio …    [Copy]       │
│   Claude Code (.mcp.json, token from HIO_MCP_TOKEN)              [Copy]       │
│   Claude Desktop (uses the hio command, not this window)         [Copy]       │
│ Permissions                                Ask        Always allow           │
│   Flash firmware (flash, LTE, raw flash)    (•)        ( )                    │
│   Erase and recover                         (•)        ( )                    │
│   Write memory                              (•)        ( )                    │
│   Write UICR / factory data                 (•)        ( )                    │
│   Connect new devices (opens a tab)         (•)        ( )                    │
│   Allowed until disconnect: Claude Code — Flash firmware   [Revoke]          │
│ Activity (last 1000)                                              [Clear]    │
│   14:02:11  Claude Code  flash     jlink:6830…  app.hex      ok     12.4 s    │
│   14:01:58  Claude Code  send_command jlink:6830…  "lte state"  ok   0.3 s   │
└──────────────────────────────────────────────────────────────────────────────┘
```

- **Enable** is off by default. Turning it on creates the token if needed and starts the server. The state text
  is one of "Off", "Listening", "Listening · 2 clients", or an error ("Port 47800 is in use by another program —
  choose another port", `mcp.port_in_use`) with **Retry**.
- **Port:** a spin box (1024–65535). A change restarts the server; clients have to be reconfigured, and a note
  says so.
- **Token:** masked, with Show and Copy. **Regenerate…** asks "Regenerate the token? Connected AI clients stop
  working until you update their configuration." and then restarts the server. The token is never shown in the
  activity list or the logs.
- **Snippets:** Claude Code HTTP (with the token embedded), `.mcp.json` using `${HIO_MCP_TOKEN}`, and the Claude
  Desktop stdio entry using the installed `hio` path (ADR-0106). Copy shows a toast: "Copied. Run it in a
  terminal."
- **Permissions:** one row per consent category (ADR-0101). The "Allowed until disconnect" list shows the current
  remembered grants, each with Revoke.
- **Activity:** a table (newest first), 1000 entries (ADR-0105). Rows can be selected; Copy copies them as text.
  Clear empties the list. Errors show their code with the message as a tooltip.

### Rail indicator

Above Settings at the bottom of the rail sits an "MCP" icon with a state dot: hidden when off; green when
listening; green with a count badge when clients are active (a request in the last 60 s); red on error. Its
tooltip is e.g. "AI assistants: listening on port 47800, 1 client". Clicking it opens Settings → AI assistants.

### Consent prompt window (ADR-0101, ADR-0108)

```
┌ HARDWARIO Monitor — Allow AI assistant? ──────────────────────┐
│  Claude Code wants to flash firmware                          │
│  Device    CHESTER app · 683012345 (tab 1)                     │
│  File      /home/me/proj/build/merged.hex  (412 KiB)           │
│  Target    Application MCU (app)                               │
│                                                               │
│  Show device ↗                                                │
│  [Deny]  [Allow once]  [Allow flashing for Claude Code ▸]      │
│  Asks again in 1:54 if not answered — then it is denied.       │
└───────────────────────────────────────────────────────────────┘
```

- The prompt is **its own top-level window**, like 1Password's SSH-agent prompt (ADR-0108):
  - frameless, always on top, with no parent, so it also appears while the main window is minimised or on another
    virtual desktop (shown on all workspaces where Qt supports it);
  - centred on the screen under the mouse cursor;
  - raised with `requestActivate()`, while the main window's taskbar entry flashes (Windows/Linux) or the dock
    icon bounces (macOS);
  - on Windows and Wayland the OS may refuse keyboard focus. The window is still shown on top, and nothing tries to
    work around the OS rules.
- The main window does not switch tabs by itself. **Show device ↗** raises the main window on the device's tab, and
  the prompt stays open.
- One prompt at a time, at most 8 queued (more → `mcp.consent_timeout` immediately). A countdown runs for 120 s.
- The app does **not** stay in the tray. Closing the main window quits, which denies all pending prompts and stops
  the server (ADR-0108).
- Detail rows per category: flash (file and size, or catalog id; target; "contains UICR data" in the warning
  colour); erase (scope, with "The PIB (serial number, keys) is lost" for `all`); memory-write (address range,
  size); uicr ("Factory data of the device may be overwritten"); connect ("Open a new tab for J-Link 683012345").
- When the request is withdrawn (the client cancelled or disconnected), the prompt closes with a toast "The AI
  assistant cancelled the request".
- Keyboard: Esc = Deny. There is no default button, so Enter does nothing until a button has focus.

### Device tabs

- AI-opened tabs carry an "AI" badge next to the title (tooltip "Opened by Claude Code").
- **Markers** in the console (ADR-0105): "— AI (Claude Code): flash merged.hex —", "— AI: flash done in 12.4 s —",
  "— AI: flash failed: <message> —".
- While an AI `send_command` runs, the console input shows the placeholder hint "AI is running a command…". Input
  stays enabled.
- **AI operations** set the tab busy through `setBusy` (0004): rail badge on the page "AI", busy text
  "AI: Flashing… 43 %", and a Cancel button in the device bar. Cancel cancels the operation, and the tool returns
  `cancelled`. Closing the tab or quitting asks the existing busy question ("An AI assistant is flashing. Cancel it
  and close the tab?").
- A user operation running on a tab makes AI operation tools return `internal.busy`; console tools still work.

### Texts

All new texts are in English source + Czech (ADR-0027). MCP wire texts stay English (ADR-0103).

## CLI

None (the CLI side is in 0013).

## Design

### Modules

```
app/gui/src/mcp/   mcp_controller.{h,cpp}  gui_device_provider.{h,cpp}  gui_consent_broker.{h,cpp}
                   activity_model.{h,cpp}  consent_rules.{h,cpp}  client_snippets.{h,cpp}
app/gui/src/viewmodels/  mcp_settings_view_model.{h,cpp}  consent_view_model.{h,cpp}
app/gui/qml/settings/McpSection.qml   qml/mcp/ConsentWindow.qml  McpRailIndicator.qml  ActivityTable.qml
app/gui/tests/     mcp_*_test.cpp, qml/tst_mcp_*.qml
```

The section is registered as a **contributed section** of the Settings page (0006), like Console (0005). The rail
indicator and the tab "AI" badge amend 0004 (shared components change only through 0004 or an amendment,
ADR-0059; this plan is that amendment for these two items).

### Classes (sketch, `hio::gui`)

```cpp
class McpController : public QObject {          // owned by AppViewModel, main thread
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)          // Off, Starting, Listening, Error
    Q_PROPERTY(int activeClients READ activeClients NOTIFY activeClientsChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
public:
    McpController(AppViewModel&, SettingsStore&, hio::mcp::TokenStore);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void regenerateToken();
    void shutdown();                             // on quit: stop server, cancel calls, close AI-only resources
private:
    std::unique_ptr<GuiDeviceProvider> provider_;
    std::unique_ptr<GuiConsentBroker> consent_;
    std::unique_ptr<ActivityModel> activity_;   // also IActivitySink
    std::unique_ptr<hio::mcp::McpServer> server_;
    std::unique_ptr<hio::mcp::HttpServer> http_;
};

class GuiDeviceProvider final : public hio::mcp::IDeviceProvider {
    // list(): the open tabs (id = tab's SessionKey string) + free probes from ProbeScanner's last result
    //         + recently seen BLE devices (0012) — no extra enumeration.
    // open(): consent `connect` → AppViewModel::openTab(probe/ble, profile, target, options, openedBy=client)
    //         → done after the first connection attempt (connectionChanged), or the tab's error.
    // close(): only tabs with openedBy set → closes the tab; else mcp.tool_not_allowed (GUI variant text).
    // beginBusy/endBusy → DeviceTabViewModel::setBusy("ai", text, confirmText, cancel).
    // marker → the tab's ConsoleLog::appendMarker (translated text, ADR-0105).
};

class GuiConsentBroker final : public hio::mcp::IConsentBroker {
    // Rules: SettingsStore mcp/consent/<category> = ask|allow; grants: map clientKey → categories,
    // dropped on clientGone (legacy session end / 30 min idle) or Revoke. Queue ≤ 8, 120 s timeout.
};

class ActivityModel : public QAbstractListModel, public hio::mcp::IActivitySink {   // ring of 1000 entries
};
```

- `DeviceTabViewModel` gains `openedBy` (`std::optional<ClientInfo>`, not persisted). AI-opened tabs are **not**
  restored on the next start (`ui/tabs` skips them).
- `AppViewModel::openTab` gains an overload for programmatic opening (probe SN or BLE id, profile, target,
  options). It reuses the Choose-device validation (in use, 8-tab limit → `internal.session_limit`).

### Data flow and threading

Everything is on the main thread (ADR-0097). `HttpServer` runs on the main event loop; tool handlers read the tab's
`ConsoleLog` directly and call session APIs. The prompt window is QML on the same thread; the broker's callbacks fire from
QML button handlers. No new threads.

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `mcp/enabled` | bool | `false` |
| `mcp/port` | int 1024–65535 | `47800` |
| `mcp/consent/flash` · `…/erase` · `…/memoryWrite` · `…/uicr` · `…/connect` | `ask`\|`allow` | `ask` |

The token is **not** a setting. It lives in `<AppConfigLocation>/mcp-token` (ADR-0102). The diagnostic report
(0007, ADR-0075) excludes that file and lists only `mcp/*` keys.

### Error cases (GUI-originated)

| Case | Code | Shown |
|---|---|---|
| Port in use / listen failed | `mcp.port_in_use` / `mcp.listen_failed` | section state text + Retry; rail dot red |
| Token file unreadable / wrong owner | `mcp.token_file` | section ErrorPanel with "Regenerate token" |
| Consent prompt not answered | `mcp.consent_timeout` | to the AI; activity row |
| Tab limit reached on `connect` | `internal.session_limit` | to the AI; no prompt |
| User closed the AI's tab mid-call | `mcp.device_not_open` | to the AI; marker not needed (tab gone) |

### Limits

Activity 1000 entries · consent queue 8, timeout 120 s · remembered grants until client end or 30 min idle · active
client = request in the last 60 s · rail tooltip refresh ≤ 1/s.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| GUI consent | ask + remember per client / always ask / settings only | ask, remember per client | ADR-0101 |
| Activity display | markers + panel + indicator / panel / markers | markers + MCP section list + rail indicator | ADR-0105 |
| Shared shell | no lock / lock input | no lock, hint in the input | ADR-0105 |
| Where the prompt appears | own on-top window (1Password-style) / + notification / native notification buttons / in-window dialog | own always-on-top window + taskbar/dock alert | ADR-0108 |
| Keep running in the tray | no / optional / always with MCP | no, closing the window quits | ADR-0108 |
| Token storage | owner-only file / INI / keychain | owner-only file | ADR-0102 |
| Port | 47800 fixed / 8090 / next free | 47800, error if busy | ADR-0102 |
| Minor defaults: section name "AI assistants (MCP)" and its layout; indicator on the rail above Settings (no header bar exists); AI-opened tabs get an "AI" badge and are not restored; the prompt does not switch tabs (Show device link); Esc = Deny and no default button; snippet set (Claude Code HTTP, `.mcp.json`, Claude Desktop stdio); settings keys above; activity "active client" = request within 60 s | accept / change | _pending_ | — |

## Implementation steps

Phase A — controller and provider
- [ ] A0 Prerequisite check: 0013 phases A–C and 0004/0005/0006 implemented (otherwise stop and report)
- [ ] A1 `McpController` (start/stop on settings, port change, token, state) + tests
- [ ] A2 `GuiDeviceProvider` (list, open with consent, close rules, busy, markers, hint), `openTab` overload,
  `openedBy`, tab restore skip + tests
- [ ] A3 `GuiConsentBroker` + `ConsentViewModel` (rules, grants, queue, timeout, withdraw) + tests
- [ ] A4 `ActivityModel` (ring, roles, copy) + tests

Phase B — QML
- [ ] B1 `McpSection.qml` (+ `McpSettingsViewModel`, snippets with the installed `hio` path)
- [ ] B2 `ConsentWindow.qml` (flags, screen choice, alert, Show device); `McpRailIndicator.qml`; tab "AI" badge; console input hint
- [ ] B3 Mockups (light/dark) in `plan/gui/mockups/` (Git LFS for images); Czech translations; accessibility
  (names, focus order, screen-reader announcement of the prompt)

Phase C — verification
- [ ] C1 GUI integration test: in-process HTTP client → flash on a fake tab → prompt window → Allow once → marker,
  activity, busy, result
- [ ] C2 Manual (by the user): Claude Code against the GUI on Linux, Windows, macOS; token file permissions per OS;
  port in use; regenerate token; quit while an AI flash runs; the prompt window over other apps with the
  main window minimised and on another virtual desktop (Windows 10/11, macOS, GNOME X11, GNOME and KDE Wayland;
  record where focus is not given)
- [ ] C3 Update roadmap, `0004-mcp.md`

## Tests and verification

- **Unit (GTest):** controller state machine (off → listening → error → retry; port change restarts; regenerate
  restarts and invalidates); provider ids and listing from tabs and the scanner; `connect` → consent → tab opened
  with `openedBy`; `connect` denied; 8-tab limit; `disconnect` on a user tab → `mcp.tool_not_allowed`; tab closed
  mid-call → `mcp.device_not_open`; busy set/cleared on success, failure and cancel; markers for marked tools only;
  consent rules (allow → no prompt), grants per client and category, revoke, `clientGone`, queue overflow,
  120 s timeout (fake clock), withdrawal on cancel; activity ring at 1000.
- **Qt Quick Test:** MCP section in light/dark, enabled/disabled/error states; consent window for each category
  (detail rows, countdown, Esc = Deny, window flags set, no transient parent, screen chosen from the cursor
  position with a fake screen list, alert called once); rail indicator states; layout at 960 × 600 in Czech (geometry checks as
  0006).
- **Integration:** C1 above, plus two concurrent clients (one denied, one allowed); 32 in-flight `wait_for_state`
  calls do not stall the UI (frame time < 50 ms measured).
- **Sanitizers / tidy / qmllint / translations** as in M3.
- **Manual (by the user, before Done):** C2.

## Risks and open points

- **Focus on Windows / Wayland:** the OS may show the prompt without focus (ADR-0108). The manual checks record
  the actual behaviour per platform; native notifications are reconsidered in M7.
- **Prompts while the user works**: a burst of AI requests could interrupt the user. The queue limit,
  "Allow … for <client>" and per-category Always allow keep this bounded; feedback from the beta may tune it.
- **Programmatic tab opening** bypasses the Choose-device page; it reuses the same validation code, so the
  behaviour stays the same (tests cover in-use and limit).
- **Claude Desktop** cannot use the GUI's HTTP server without a bridge. The snippet points to `hio mcp` (stdio),
  which runs separately and cannot share the GUI's probe (probe locks). The section says this in one line.

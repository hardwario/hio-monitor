// The debug_device prompt (ADR-0107, plan/features/0013 C3): rttt's workflow text adapted to the
// tools of this server, CHESTER targets, device ids, consent errors and the rttt tool-name mapping.
// English (ADR-0103); no upload endpoint (ADR-0100).

#include "hio/core/result.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "tools/tools.h"

#include <QString>
#include <QStringLiteral>

#include <map>

namespace hio::mcp::tools::detail {

namespace {

constexpr const char* debugDeviceText =
    "You are connected to HARDWARIO Monitor, which drives embedded devices (CHESTER: Zephyr / "
    "nRF Connect SDK) over a J-Link probe (RTT console and log) or Bluetooth (shell only).\n"
    "\n"
    "Devices:\n"
    "- `list_devices` shows the devices and their ids: \"jlink:<probe serial>\" or "
    "\"ble:<id>\". `connect` opens one (the user may have to allow it in HARDWARIO Monitor), "
    "`disconnect` closes it.\n"
    "- Every device tool takes `device`; leave it out when exactly one device is connected.\n"
    "- CHESTER has two MCUs (targets): \"app\" (nRF52840, application, RTT console) and \"lte\" "
    "(nRF9160, LTE modem; no console). Target tools default to the console's target (\"app\"); "
    "pass target=\"lte\" for the modem MCU.\n"
    "- Bluetooth devices have the shell only: target, firmware and probe tools return "
    "mcp.capability_missing for them.\n"
    "\n"
    "Inspect what the firmware is doing:\n"
    "- `read_log` shows buffered device logs; filter with `pattern`, or pass `after_cursor` "
    "(log_cursor from a send_command result, log_cursor_end from flash / reset / wait_for_state) "
    "to see only newer logs.\n"
    "- `send_command` talks to the device shell (try `help`, `version`, `kernel uptime`, "
    "`lte state`). It returns at the shell prompt or after a short silence; increase `timeout` "
    "for slow commands. Output belongs to the sent command only; logs are separate (read_log). "
    "interleaved=true means the user typed a command at the same time.\n"
    "- `read_terminal` shows the shell history, including the user's commands and notes "
    "(dir \"marker\") such as the AI actions.\n"
    "- `wait_for_state` waits for a line matching a regular expression (optionally sending a "
    "command every poll_interval seconds), e.g. pattern=\"attached: yes\" with "
    "command=\"lte state\" after a reset. A timeout is a normal result (status \"timeout\").\n"
    "- `status` shows the connection state and buffer counters of the devices.\n"
    "\n"
    "Flash new firmware:\n"
    "- `flash` with `path` (an absolute path to a .hex file on the machine that runs HARDWARIO "
    "Monitor, e.g. <project>/build/merged.hex) or `catalog_id` (a 32-digit HARDWARIO firmware "
    "id). There is no upload: if the file is on another machine, copy it to this one first. The "
    "device reboots and the console re-attaches by itself. Call `wait_for_connection`, then "
    "verify with `send_command` (e.g. `version`) or read_log with after_cursor=log_cursor_end "
    "from the flash result (the older output is still in the buffer).\n"
    "- `flash_lte` updates the LTE modem firmware (several minutes), `erase` and `recover` erase "
    "a target.\n"
    "\n"
    "Low-level debugging (use sparingly, prefer shell and logs):\n"
    "- `target_status` shows whether the CPU runs; `halt` stops it (the console keeps working), "
    "`go` resumes, `reset` reboots (halt=true stays at the reset vector).\n"
    "- `read_registers` needs a halted CPU (call `halt` first); PC/LR/SP tell you where the "
    "firmware is stuck.\n"
    "- `read_memory` reads RAM, peripherals and flash live (to_file for large blocks); "
    "`write_memory` writes RAM and peripheral registers only (it can still make the device run "
    "arbitrary code, which can also erase its flash and the PIB); `write_flash` programs raw "
    "flash bytes (the device reboots). `read_pib` decodes the Product Information Block.\n"
    "- `memory_zones` lists the J-Link memory zones.\n"
    "\n"
    "Permissions:\n"
    "- flash, flash_lte, write_flash, erase, recover, write_memory (and UICR writes) need the "
    "user's permission. `erase` with scope \"all\" and `recover` also remove the Product "
    "Information Block and need both erase and uicr; erase with scope \"code\" needs erase "
    "only. mcp.tool_not_allowed means the server was started without it (the CLI needs `hio mcp "
    "--allow flash,erase,memory-write,uicr` or `--allow all`; the error's suggestion names the "
    "items, e.g. `--allow erase,uicr`); "
    "mcp.consent_denied / mcp.consent_timeout mean the user declined or did not answer in "
    "HARDWARIO Monitor. Ask the user instead of retrying.\n"
    "- internal.busy means another operation (named in the error detail) runs on the device; "
    "wait and retry.\n"
    "\n"
    "Troubleshooting:\n"
    "- Commands fail or no output arrives: call `reconnect` (re-attaches RTT without resetting "
    "the device), then `wait_for_connection` and retry. If RTT attaches to a stale control block "
    "after a reflash, pass rtt_address (the _SEGGER_RTT address from the ELF).\n"
    "- Still dead: `reset`, wait_for_connection, retry `send_command`.\n"
    "- Another tool (J-Link Commander, west flash) needs the probe: `release_probe`, then "
    "`reacquire_probe` afterwards.\n"
    "- The shell echoes garbage right after boot: wait 2-3 s and retry.\n"
    "\n"
    "Tool names of rttt map as follows: jlink_close -> release_probe, jlink_open -> "
    "reacquire_probe; the other rttt tools keep their names and take the extra `device` "
    "argument.\n";

} // namespace

QString debugDevicePromptText() { return QString::fromUtf8(debugDeviceText); }

hio::Status registerPrompts(hio::mcp::Registry& registry) {
    return registry.addPrompt(hio::mcp::PromptSpec{
        .name = "debug_device",
        .title = "Debug a device",
        .description = "How to debug a connected HARDWARIO device with this server's tools.",
        .arguments = {},
        .get = [](const hio::mcp::RequestContext&,
                  const std::map<QString, QString>&) -> hio::Result<hio::mcp::PromptResult> {
            return hio::mcp::PromptResult{
                .description = QStringLiteral("Debug a HARDWARIO device"),
                .messages = {hio::mcp::PromptMessage{.role = QStringLiteral("user"),
                                                     .text = debugDevicePromptText()}}};
        }});
}

} // namespace hio::mcp::tools::detail

#pragma once

// JLinkProbe - one J-Link probe and its target connection over an IJLinkApi
// (plan/features/0002-m1-jlink-rtt.md). Owned by the session thread (ADR-0031).
//
// Error mapping (cable diagnosis, 0001-architecture): no probe -> probe.not_found; the library
// cannot open it -> probe.open_failed; the target does not connect and VTref is below the
// profile's minimum -> target.no_power (a reading of exactly 0 mV only after a healthy reading;
// review R21); otherwise target.connect_failed. The library's error
// texts are appended to `detail`.
//
// Thread safety: none (one owner thread, like the IJLinkApi it uses).

#include "hio/core/result.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/types.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::jlink {

// How JLinkProbe::open() configures the library.
enum class OpenMode : std::uint8_t {
    Normal,         // automatic firmware updates disabled (review fix R1)
    FirmwareUpdate, // used inside updateFirmware(): DisableAutoUpdateFW is not sent (review R11)
};

struct ProbeTiming {
    int connectAttempts = 3;
    std::chrono::milliseconds connectRetryDelay{500};
    int resetDelayMs = 10; // JLINKARM_SetResetDelay before a reset (rttt: reset(ms=10))
};

class JLinkProbe {
public:
    JLinkProbe(IJLinkApi& api, Clock& clock, ProbeTiming timing = {});

    // The USB probes the library sees (network J-Links are not used for sessions; review R12).
    // Errors: probe.open_failed.
    [[nodiscard]] Result<std::vector<ProbeInfo>> list();

    // Opens the probe with `serialNumber`, or the first listed probe when nullopt, suppresses every
    // SEGGER dialog and disables the automatic firmware update (before and after Open; never
    // updates the firmware). Errors: probe.not_found, probe.open_failed.
    // A serial number of a network J-Link is probe.not_found ("not supported for sessions yet");
    // that check lists the network J-Links only on the first failed open of a serial number
    // (review R22).
    [[nodiscard]] Result<ProbeInfo> open(std::optional<std::uint32_t> serialNumber,
                                         OpenMode mode = OpenMode::Normal);
    // Closes the probe (no-op when not open).
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return m_open; }
    // The open probe (serial number, product, firmware string).
    [[nodiscard]] const ProbeInfo& info() const noexcept { return m_info; }

    // The SWD speed: a fixed kHz, or nullopt = automatic (JLINKARM_SetSpeed(0), the default;
    // ADR-0090). Applied now and again by connect() right after `Device =`, as pylink's
    // JLink.connect() does.
    void setSpeed(std::optional<int> kiloHertz);
    // Errors: probe.open_failed.
    [[nodiscard]] Status selectSwd();

    // Connects the target MCU `deviceName` (e.g. "NRF52840_xxAA"): `Device = <name>`, the speed
    // of setSpeed() (automatic by default), then up to connectAttempts attempts, connectRetryDelay
    // apart. Errors: target.no_power (0 < VTref < minTargetMillivolts,
    // or VTref = 0 when `healthyVoltageSeen`: a reading >= minTargetMillivolts was seen on this
    // probe in this session; a probe that cannot measure VTref reports 0), target.connect_failed,
    // cancelled (stop requested).
    [[nodiscard]] Status connect(std::string_view deviceName, int minTargetMillivolts,
                                 bool healthyVoltageSeen, const std::stop_token& stop);

    // Target voltage. Errors: probe.io_failed (the open probe does not answer: a USB glitch or
    // an unplugged probe; review R52).
    [[nodiscard]] Result<HardwareStatus> hardwareStatus();
    // Unknown when the library cannot tell (logged).
    [[nodiscard]] FirmwareStatus firmwareStatus();
    // The probe must be open in OpenMode::Normal: its status there is the status "before" (review
    // R24). When that is not Current, the probe is reopened in OpenMode::FirmwareUpdate, updated
    // (Outdated, Unknown) or synchronized (Newer) with the library's firmware, closed, opened
    // again normally and the status re-read (review R11). Returns the status before the update
    // (Current: nothing was done). Errors: probe.firmware_update_failed (still not Current),
    // those of open().
    [[nodiscard]] Result<FirmwareStatus> updateFirmware();

    // Resets the target; `halt` = false lets the CPU run again. Errors: target.connect_failed.
    [[nodiscard]] Status reset(bool halt);
    [[nodiscard]] Status halt();
    void go();

private:
    [[nodiscard]] std::string libraryDetail(std::string detail);
    void execCommands(std::span<const std::string_view> commands);
    void applySpeed();
    [[nodiscard]] bool isNetworkProbe(std::uint32_t serialNumber);

    IJLinkApi& m_api;
    Clock& m_clock;
    ProbeTiming m_timing;
    ProbeInfo m_info;
    bool m_open = false;
    std::optional<int> m_speed; // kHz; nullopt = automatic (ADR-0090)
    // The result of the network-J-Link check of a serial number (isNetworkProbe, review R22).
    std::optional<std::pair<std::uint32_t, bool>> m_networkCheck;
};

} // namespace hio::jlink

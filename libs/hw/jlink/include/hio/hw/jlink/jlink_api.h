#pragma once

// IJLinkApi - a typed C++ view of the JLinkARM library functions we use (ADR-0028).
//
// JLinkLibrary implements it over a runtime-loaded copy of the vendor library (ADR-0006,
// ADR-0030); unit tests mock it (GMock) or fake it. All higher code (JLinkProbe, RttChannel,
// libs/device) depends only on this interface.
//
// The interface is deliberately thin: it reports the vendor's results (negative return codes,
// error texts) without interpreting them. Mapping them to hio::Error is done by JLinkProbe and
// RttChannel, so the rules can be unit-tested.
//
// Thread safety: none. An instance belongs to exactly one thread (a session worker, or the
// caller of a short-lived enumeration); every vendor call for one library copy is made there.

#include "hio/hw/jlink/types.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::jlink {

// Hosts listProbes() looks at (JLINKARM_EMU_GetList HostIFs). Sessions and every probe open use
// USB only; `hio jlink list` shows both (review R12, decided 2026-10-10).
enum class ProbeHosts : std::uint8_t {
    Usb,
    UsbAndIp,
};

// Target interface for selectInterface() (JLINKARM_TIF_Select).
enum class TargetInterface : std::uint8_t {
    Jtag,
    Swd,
};

// Progress reported by the library's flash loader during endDownload(): the action ("Erase",
// "Program", "Verify", "Compare", ...) and a percentage. Called on the calling thread; must not
// throw.
using FlashProgressFn = std::function<void(std::string_view action, int percent)>;

class IJLinkApi {
public:
    IJLinkApi() = default;
    virtual ~IJLinkApi() = default;
    IJLinkApi(const IJLinkApi&) = delete;
    IJLinkApi& operator=(const IJLinkApi&) = delete;
    IJLinkApi(IJLinkApi&&) = delete;
    IJLinkApi& operator=(IJLinkApi&&) = delete;

    // ---- library ----------------------------------------------------------------------------
    // JLINKARM_GetDLLVersion: major * 10000 + minor * 100 + revision.
    [[nodiscard]] virtual std::uint32_t libraryVersion() = 0;
    // JLINKARM_GetCompileDateTime.
    [[nodiscard]] virtual std::string compileDateTime() = 0;

    // ---- probes -----------------------------------------------------------------------------
    // JLINKARM_EMU_GetList over the given hosts. Error: the negative vendor return code.
    [[nodiscard]] virtual std::expected<std::vector<ProbeInfo>, int>
    listProbes(ProbeHosts hosts) = 0;
    // JLINKARM_EMU_SelectByUSBSN. Returns the vendor code (< 0: no probe with this SN).
    [[nodiscard]] virtual int selectBySerialNumber(std::uint32_t serialNumber) = 0;
    // JLINKARM_Open. nullopt on success, otherwise the library's error text.
    [[nodiscard]] virtual std::optional<std::string> open() = 0;
    // JLINKARM_Close.
    virtual void close() = 0;
    // JLINKARM_IsOpen.
    [[nodiscard]] virtual bool isOpen() = 0;
    // JLINKARM_ExecCommand. Returns the library's error text (empty on success).
    [[nodiscard]] virtual std::string execCommand(std::string_view command) = 0;
    // JLINKARM_TIF_Select. Returns the vendor code (0 = success).
    [[nodiscard]] virtual int selectInterface(TargetInterface targetInterface) = 0;
    // JLINKARM_SetSpeed (kHz).
    virtual void setSpeed(std::uint32_t kiloHertz) = 0;
    // JLINKARM_Connect. Returns the vendor code (< 0: error).
    [[nodiscard]] virtual int connect() = 0;
    // JLINKARM_IsConnected.
    [[nodiscard]] virtual bool isConnected() = 0;
    // JLINKARM_GetHWStatus. nullopt when the probe does not answer.
    [[nodiscard]] virtual std::optional<HardwareStatus> hardwareStatus() = 0;
    // JLINKARM_GetFirmwareString: the open probe's firmware identification string.
    [[nodiscard]] virtual std::string firmwareString() = 0;
    // JLINKARM_GetEmbeddedFWString: the firmware bundled in the library for `firmwareId`.
    // Error: the negative vendor return code.
    [[nodiscard]] virtual std::expected<std::string, int>
    embeddedFirmwareString(std::string_view firmwareId) = 0;
    // JLINKARM_UpdateFirmwareIfNewer: checksum of the new firmware, 0 when unchanged.
    [[nodiscard]] virtual std::uint32_t updateFirmwareIfNewer() = 0;
    // JLINKARM_GetSN: serial number of the open probe.
    [[nodiscard]] virtual std::uint32_t serialNumber() = 0;

    // ---- target -----------------------------------------------------------------------------
    // JLINKARM_SetResetDelay (ms).
    virtual void setResetDelay(int milliseconds) = 0;
    // JLINKARM_Reset. Returns the vendor code (< 0: error). The CPU is halted afterwards.
    [[nodiscard]] virtual int reset() = 0;
    // JLINKARM_Halt. Returns the vendor code (0 = halted).
    [[nodiscard]] virtual int halt() = 0;
    // JLINKARM_Go.
    virtual void go() = 0;

    // ---- RTT (JLINK_RTTERMINAL_*) -----------------------------------------------------------
    // Control(START) with automatic control block search. Returns the vendor code (< 0: error).
    [[nodiscard]] virtual int rttStart() = 0;
    // Control(STOP). Returns the vendor code.
    virtual int rttStop() = 0;
    // Control(GETNUMBUF). < 0 while the control block has not been found yet.
    [[nodiscard]] virtual int rttBufferCount(RttDirection direction) = 0;
    // Control(GETDESC). Error: the negative vendor return code.
    [[nodiscard]] virtual std::expected<RttBufferInfo, int>
    rttBufferInfo(int index, RttDirection direction) = 0;
    // Read: bytes read (>= 0) or a negative vendor code.
    [[nodiscard]] virtual int rttRead(int index, std::span<std::byte> buffer) = 0;
    // Write: bytes written (>= 0, may be less than requested) or a negative vendor code.
    [[nodiscard]] virtual int rttWrite(int index, std::span<const std::byte> data) = 0;

    // JLINK_SetHookUnsecureDialog with a hook that always answers NO (review R1): the library never
    // mass-erases a read-back protected chip on its own. Returns the vendor code (< 0: error).
    [[nodiscard]] virtual int installUnsecureDialogHook() = 0;

    // ---- memory and flash (M2, plan/features/0003) ------------------------------------------
    // JLINKARM_BeginDownload(0): following writeMemory() calls go to the flash loader's buffer.
    virtual void beginDownload() = 0;
    // JLINKARM_WriteMem. Bytes accepted (>= 0) or a negative vendor code.
    [[nodiscard]] virtual int writeMemory(std::uint32_t address,
                                          std::span<const std::byte> data) = 0;
    // JLINKARM_EndDownload: the loader erases the touched sectors and programs them. `progress`
    // receives the loader's progress callbacks during the call. Returns the vendor code (< 0:
    // error).
    [[nodiscard]] virtual int endDownload(const FlashProgressFn& progress) = 0;
    // JLINKARM_ReadMemEx: bytes read (>= 0) or a negative vendor code.
    [[nodiscard]] virtual int readMemory(std::uint32_t address, std::span<std::byte> buffer) = 0;
    // JLINKARM_ReadMemU32 of one word. Error: the vendor code (or the item status).
    [[nodiscard]] virtual std::expected<std::uint32_t, int> readU32(std::uint32_t address) = 0;
    // JLINK_EraseChip: erases flash and UICR. Returns the vendor code (< 0: error).
    [[nodiscard]] virtual int eraseChip() = 0;
    // JLINKARM_CORESIGHT_Configure("") (SWD). Returns the vendor code (< 0: error).
    [[nodiscard]] virtual int coresightConfigure() = 0;
    // JLINKARM_CORESIGHT_ReadAPDPReg: register `index` (0..3, A[3:2]) of the selected AP (`ap`)
    // or of the DP. Error: the negative vendor code.
    [[nodiscard]] virtual std::expected<std::uint32_t, int> coresightRead(std::uint8_t index,
                                                                          bool ap) = 0;
    // JLINKARM_CORESIGHT_WriteAPDPReg. Returns the vendor code (< 0: error).
    [[nodiscard]] virtual int coresightWrite(std::uint8_t index, bool ap, std::uint32_t value) = 0;

    // ---- library output (log / warning / error out handlers) --------------------------------
    // Logs the messages the library reported since the last call into the hio.jlink category.
    virtual void flushMessages() = 0;
    // flushMessages(), then returns the library's error texts reported since the previous call
    // (newest last, "; "-separated, bounded) for the `detail` of the next hio::Error.
    [[nodiscard]] virtual std::string takeErrorText() = 0;
};

} // namespace hio::jlink

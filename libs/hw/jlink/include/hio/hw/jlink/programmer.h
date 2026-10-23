#pragma once

// IProgrammer - programming an MCU through one probe (plan/features/0003-m2-flashing.md,
// ADR-0015, ADR-0035, ADR-0039). JLinkProgrammer implements it on IJLinkApi; it is used only on
// the session worker (ADR-0031), after the worker connected the target.
//
//   program  BeginDownload -> WriteMem in writeChunk pieces -> EndDownload (the flash loader
//            erases the touched sectors and programs them; < 0 -> target.program_failed)
//   verify   ReadMemEx of every written range in verifyChunk pieces, byte compare (first
//            mismatch -> firmware.verify_failed, the address in `detail`)
//   eraseCode  0xFF over the flash range through the flash loader (sectors that differ are
//            erased; UICR untouched; confirmed on an nRF52840 in Phase 0, G1 EraseCode gate).
//   eraseAll reset + halt, then JLINK_EraseChip (flash + UICR; HIL H5: after only a halt it
//            erased nothing on a running nRF52840)
//   recover  checks the expected family's CTRL-AP IDR, then CTRL-AP ERASEALL via CoreSight, poll
//            ERASEALLSTATUS, CTRL-AP RESET toggle
//   target operations (M6, plan/features/0013, ADR-0099, ADR-0158): halt / go (JLINKARM_Halt /
//            Go), isHalted, coreId, readMemory with an access width (8: ReadMemEx, 16: ReadMemEx
//            with access width 2, 32: ReadMemU32 with a count), writeMemory with a width (8:
//            JLINKARM_WriteMem outside a download, 16 / 32: WriteU16 / WriteU32 per unit), units
//            little-endian; readRegisters (the core must be halted), memoryZones. A missing
//            optional library function (ADR-0151) -> dependency.jlink_function_missing (detail:
//            the function name).
//   identify FICR INFO.PART / INFO.VARIANT; when FICR cannot be read, the CTRL-AP of the expected
//            family (a Nordic CTRL-AP IDR at its AP index first, then APPROTECTSTATUS); a Nordic
//            CTRL-AP at another family's AP index only names that family (never "protected")
//
// Cancellation: `stop` is checked before a download starts and between verify chunks. The
// buffering of one download, EndDownload, EraseChip and an ERASEALL are never interrupted (a
// buffered download cannot be discarded through the library). Deadlines (timing below) are
// checked between chunks; a single library call is synchronous. A download that fails or runs out
// of time before EndDownload is abandoned by closing the probe (review R3); the library has no
// abort, and Phase 0 (item 0.10) showed that the close still programs the buffered data, so this is
// reported as an interrupted flash: target.program_failed, "may be partly written" (ADR-0109). The
// session reopens the probe.
//
// Thread safety: none (the session worker).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hio::jlink {

enum class ResetMode : std::uint8_t {
    Go,   // reset and let the CPU run
    Halt, // reset and keep the CPU halted
};

// What identify() found.
struct ChipInfo {
    std::uint32_t part = 0;     // FICR INFO.PART (0x52840, 0x9160, 0x9120, ...); 0 when protected
    std::uint32_t variant = 0;  // FICR INFO.VARIANT (ASCII, e.g. "AAD0")
    bool protectedChip = false; // CTRL-AP reports APPROTECT enabled; FICR not readable
    std::string name;           // display name ("nRF52840"), filled by the device layer
    // The family that answered (ChipFamily::name, e.g. "nRF52"): from FICR, or - when FICR cannot
    // be read (part = 0) - the family whose CTRL-AP index holds a Nordic CTRL-AP.
    std::string_view family;
};

// Where an MCU family keeps its FICR and its CTRL-AP (nRF52: FICR 0x10000100/0x10000104, CTRL-AP
// index 1; nRF91: FICR 0x00FF0140/0x00FF0144, CTRL-AP index 4 - AP indexes to confirm in
// Phase 0). `ctrlApIdr`: the CTRL-AP identification register (bank 0xF, 0xFC) compared under
// ctrlApIdrMask (revision bits 31:28 ignored): nRF52840 0x02880000, nRF91 0x12880000 (product
// specifications; to confirm in Phase 0). Review R15: under the mask both values are the same
// Nordic CTRL-AP ID, so the IDR only proves that a Nordic CTRL-AP sits at that AP index; the AP
// index (1 vs 4) is what tells the families apart. An AP that does not exist reads 0 or fails.
struct ChipFamily {
    std::string_view name; // "nRF52", "nRF91" (logs)
    std::uint32_t ficrPart = 0;
    std::uint32_t ficrVariant = 0;
    std::uint8_t ctrlApIndex = 0;
    std::uint32_t ctrlApIdr = 0;
};

inline constexpr std::uint32_t ctrlApIdrMask = 0x0FFFFFFFU;

struct ProgrammerTiming {
    std::chrono::milliseconds programTimeout{std::chrono::minutes(5)};
    std::chrono::milliseconds verifyTimeout{std::chrono::minutes(5)};
    std::chrono::milliseconds eraseAllTimeout{std::chrono::seconds(60)};
    std::chrono::milliseconds recoverTimeout{std::chrono::seconds(60)};
    std::chrono::milliseconds recoverPoll{100};
    std::size_t writeChunk = 4096;
    std::size_t verifyChunk = 16384;
};

struct ProgrammerConfig {
    // identify() tries them in order; the first one is the expected family (its CTRL-AP is used
    // by recover() and for the protection check).
    std::vector<ChipFamily> families;
    ProgrammerTiming timing;
};

// Largest readMemory() request (plan/features/0003 Bounds).
inline constexpr std::size_t maxReadMemoryBytes = 64ULL * 1024;
// Largest writeMemory() request (plan/features/0013 Bounds).
inline constexpr std::size_t maxWriteMemoryBytes = 64ULL * 1024;
// readRegisters() returns at most this many registers, memoryZones() at most this many zones
// (plan/features/0013 Bounds).
inline constexpr std::size_t maxRegisters = 128;
inline constexpr std::size_t maxMemoryZones = 32;

// The unit of a memory access (plan/features/0013); the value is the unit size in bytes.
// Re-exported as hio::device::AccessWidth.
enum class AccessWidth : std::uint8_t { Bits8 = 1, Bits16 = 2, Bits32 = 4 };

[[nodiscard]] constexpr std::size_t unitBytes(AccessWidth width) noexcept {
    return static_cast<std::size_t>(width);
}

class IProgrammer {
public:
    IProgrammer() = default;
    virtual ~IProgrammer() = default;
    IProgrammer(const IProgrammer&) = delete;
    IProgrammer& operator=(const IProgrammer&) = delete;
    IProgrammer(IProgrammer&&) = delete;
    IProgrammer& operator=(IProgrammer&&) = delete;

    // Errors: target.connect_failed (FICR unreadable and the chip is not protected).
    [[nodiscard]] virtual Result<ChipInfo> identify() = 0;
    // Errors: target.program_failed, timeout, cancelled.
    [[nodiscard]] virtual Status program(const firmware::FirmwareImage& image,
                                         const firmware::ProgressFn& progress,
                                         std::stop_token stop) = 0;
    // Errors: firmware.verify_failed, target.connect_failed (read failed), timeout, cancelled.
    [[nodiscard]] virtual Status verify(const firmware::FirmwareImage& image,
                                        const firmware::ProgressFn& progress,
                                        std::stop_token stop) = 0;
    // Errors: target.erase_failed, target.program_failed (abandoned download, ADR-0109), timeout,
    // cancelled.
    [[nodiscard]] virtual Status eraseCode(firmware::AddressRange flash,
                                           const firmware::ProgressFn& progress,
                                           std::stop_token stop) = 0;
    // Flash + UICR, after a reset and halt (HIL H5). Errors: target.connect_failed (reset failed),
    // target.erase_failed, timeout.
    [[nodiscard]] virtual Status eraseAll() = 0;
    // CTRL-AP ERASEALL. Errors: target.recover_failed, timeout, cancelled (only before it starts).
    [[nodiscard]] virtual Status recover(std::stop_token stop) = 0;
    // At most maxReadMemoryBytes, `size` a multiple of the width and `address` aligned to it;
    // 16/32-bit units are returned little-endian. Errors: usage.invalid_argument,
    // target.memory_access_failed (detail: address and the vendor code).
    [[nodiscard]] virtual Result<std::vector<std::byte>>
    readMemory(std::uint32_t address, std::size_t size, AccessWidth width) = 0;
    // Errors: target.connect_failed.
    [[nodiscard]] virtual Status reset(ResetMode mode) = 0;

    // ---- target operations (M6, plan/features/0013, ADR-0158) ----
    // At most maxWriteMemoryBytes, size and alignment as readMemory(); `data` holds the units
    // little-endian. Errors: usage.invalid_argument, target.memory_access_failed,
    // dependency.jlink_function_missing (16/32-bit units).
    [[nodiscard]] virtual Status writeMemory(std::uint32_t address, std::span<const std::byte> data,
                                             AccessWidth width) = 0;
    // JLINKARM_Halt. Errors: target.connect_failed.
    [[nodiscard]] virtual Status halt() = 0;
    // JLINKARM_Go (never fails in the library API).
    [[nodiscard]] virtual Status go() = 0;
    // Errors: target.connect_failed, dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<bool> isHalted() = 0;
    // Errors: dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<std::uint32_t> coreId() = 0;
    // At most maxRegisters, in the library's order. Errors: target.not_halted,
    // target.connect_failed, dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<std::vector<RegisterValue>> readRegisters() = 0;
    // At most maxMemoryZones (empty when the library reports none). Errors:
    // target.connect_failed, dependency.jlink_function_missing.
    [[nodiscard]] virtual Result<std::vector<MemoryZone>> memoryZones() = 0;
};

class JLinkProgrammer final : public IProgrammer {
public:
    // `api`, `probe` and `clock` must outlive the programmer.
    JLinkProgrammer(IJLinkApi& api, JLinkProbe& probe, Clock& clock, ProgrammerConfig config);

    [[nodiscard]] Result<ChipInfo> identify() override;
    [[nodiscard]] Status program(const firmware::FirmwareImage& image,
                                 const firmware::ProgressFn& progress,
                                 std::stop_token stop) override;
    [[nodiscard]] Status verify(const firmware::FirmwareImage& image,
                                const firmware::ProgressFn& progress,
                                std::stop_token stop) override;
    [[nodiscard]] Status eraseCode(firmware::AddressRange flash,
                                   const firmware::ProgressFn& progress,
                                   std::stop_token stop) override;
    [[nodiscard]] Status eraseAll() override;
    [[nodiscard]] Status recover(std::stop_token stop) override;
    static constexpr std::uint8_t ctrlApIdr = 3;         // 0x0FC IDR (bank 0xF)
    static constexpr std::uint32_t idrBank = 0xFU << 4U; // DP SELECT APBANKSEL = 0xF
    [[nodiscard]] Result<std::vector<std::byte>> readMemory(std::uint32_t address, std::size_t size,
                                                            AccessWidth width) override;
    [[nodiscard]] Status reset(ResetMode mode) override;
    [[nodiscard]] Status writeMemory(std::uint32_t address, std::span<const std::byte> data,
                                     AccessWidth width) override;
    [[nodiscard]] Status halt() override;
    [[nodiscard]] Status go() override;
    [[nodiscard]] Result<bool> isHalted() override;
    [[nodiscard]] Result<std::uint32_t> coreId() override;
    [[nodiscard]] Result<std::vector<RegisterValue>> readRegisters() override;
    [[nodiscard]] Result<std::vector<MemoryZone>> memoryZones() override;

    // CTRL-AP registers (bank 0, register index = offset / 4) [nRF52840 / nRF91 product specs].
    static constexpr std::uint8_t ctrlApReset = 0;          // 0x000 RESET
    static constexpr std::uint8_t ctrlApEraseAll = 1;       // 0x004 ERASEALL
    static constexpr std::uint8_t ctrlApEraseAllStatus = 2; // 0x008 ERASEALLSTATUS (1 = busy)
    static constexpr std::uint8_t ctrlApApProtectStatus =
        3;                                      // 0x00C APPROTECTSTATUS (bit 0: 0 = protected)
    static constexpr std::uint8_t dpSelect = 2; // DP SELECT: APSEL in bits 31:24

private:
    // Selects the CTRL-AP of `family` (bank 0). Returns the vendor code (< 0: error).
    [[nodiscard]] int selectCtrlAp(const ChipFamily& family);
    // nullopt when the CTRL-AP cannot be read.
    [[nodiscard]] std::optional<bool> isProtected(const ChipFamily& family);
    // The CTRL-AP of `family` answers with the expected IDR.
    [[nodiscard]] bool ctrlApPresent(const ChipFamily& family, std::string& detail);
    // The family whose CTRL-AP answers, starting with the expected one; nullptr when none.
    [[nodiscard]] const ChipFamily* answeringFamily(std::string& detail);
    // Closes the probe instead of calling EndDownload (review R3) and returns the
    // target.program_failed error of an interrupted flash (ADR-0109); `detail` names the reason.
    [[nodiscard]] Error abandonDownload(std::string detail);
    // One BeginDownload / WriteMem / EndDownload pass. `programsData`: the loader's non-erase
    // progress is reported as Programming (program), otherwise everything as Erasing (eraseCode).
    [[nodiscard]] Status download(const std::vector<firmware::Segment>& segments, bool programsData,
                                  ErrorCode failure, const firmware::ProgressFn& progress,
                                  const std::stop_token& stop);
    [[nodiscard]] std::string libraryDetail(std::string detail);
    // dependency.jlink_function_missing when `function` is not exported (ADR-0151).
    [[nodiscard]] Status require(OptionalFunction function);
    // target.memory_access_failed for `function` at `address` returning `code`.
    [[nodiscard]] Error memoryAccessFailed(std::string_view function, std::uint32_t address,
                                           std::size_t size, int code);

    IJLinkApi& m_api;
    JLinkProbe& m_probe;
    Clock& m_clock;
    ProgrammerConfig m_config;
};

} // namespace hio::jlink

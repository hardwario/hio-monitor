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
//            erased; UICR untouched). Erase method to confirm in Phase 0.
//   eraseAll JLINK_EraseChip (flash + UICR)
//   recover  CTRL-AP ERASEALL via CoreSight, poll ERASEALLSTATUS, CTRL-AP RESET toggle
//   identify FICR INFO.PART / INFO.VARIANT; when FICR cannot be read, CTRL-AP APPROTECTSTATUS
//
// Cancellation: `stop` is checked before a download starts and between verify chunks. The
// buffering of one download, EndDownload, EraseChip and an ERASEALL are never interrupted (a
// buffered download cannot be discarded through the library). Deadlines (timing below) are
// checked between chunks; a single library call is synchronous.
//
// Thread safety: none (the session worker).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
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
};

// Where an MCU family keeps its FICR and its CTRL-AP (nRF52: FICR 0x10000100/0x10000104, CTRL-AP
// index 1; nRF91: FICR 0x00FF0140/0x00FF0144, CTRL-AP index 4 - AP indexes to confirm in
// Phase 0).
struct ChipFamily {
    std::string_view name; // "nRF52", "nRF91" (logs)
    std::uint32_t ficrPart = 0;
    std::uint32_t ficrVariant = 0;
    std::uint8_t ctrlApIndex = 0;
};

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
    // Errors: target.erase_failed, timeout, cancelled.
    [[nodiscard]] virtual Status eraseCode(firmware::AddressRange flash,
                                           const firmware::ProgressFn& progress,
                                           std::stop_token stop) = 0;
    // Flash + UICR. Errors: target.erase_failed, timeout.
    [[nodiscard]] virtual Status eraseAll() = 0;
    // CTRL-AP ERASEALL. Errors: target.recover_failed, timeout, cancelled (only before it starts).
    [[nodiscard]] virtual Status recover(std::stop_token stop) = 0;
    // At most maxReadMemoryBytes. Errors: usage.invalid_argument, target.connect_failed.
    [[nodiscard]] virtual Result<std::vector<std::byte>> readMemory(std::uint32_t address,
                                                                    std::size_t size) = 0;
    // Errors: target.connect_failed.
    [[nodiscard]] virtual Status reset(ResetMode mode) = 0;
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
    [[nodiscard]] Result<std::vector<std::byte>> readMemory(std::uint32_t address,
                                                            std::size_t size) override;
    [[nodiscard]] Status reset(ResetMode mode) override;

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
    // One BeginDownload / WriteMem / EndDownload pass. `programsData`: the loader's non-erase
    // progress is reported as Programming (program), otherwise everything as Erasing (eraseCode).
    [[nodiscard]] Status download(const std::vector<firmware::Segment>& segments, bool programsData,
                                  ErrorCode failure, const firmware::ProgressFn& progress,
                                  const std::stop_token& stop);
    [[nodiscard]] std::string libraryDetail(std::string detail);

    IJLinkApi& m_api;
    JLinkProbe& m_probe;
    Clock& m_clock;
    ProgrammerConfig m_config;
};

} // namespace hio::jlink

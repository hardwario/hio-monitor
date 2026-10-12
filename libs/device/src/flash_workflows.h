#pragma once

// Flash, erase, recover, reset and memory-read workflows (plan/features/0003-m2-flashing.md, C2;
// ADR-0035, ADR-0036, ADR-0039). Pure logic over jlink::IProgrammer, run on the session worker
// (session_worker.cpp) and unit-tested with a GMock programmer.
//
// Flash:   image checks (ranges, PIB, UICR rule) -> Connecting: identify, family and protection
//          check, reset(Halt) -> program (Erasing / Programming) -> verify (Verifying) ->
//          reset(Go | Halt) (Resetting). With --allow-uicr the UICR page is read, the image's UICR
//          bytes are merged into it and the whole page is programmed with the code (the flash
//          loader rewrites the UICR sector; the PIB bytes come from the device; on targets whose
//          PIB is not erased with the UICR the PIB range is left out).
// Erase:   Connecting (identify) -> Erasing (code flash, or erase-all). The CPU stays halted.
// Recover: Connecting (identify; a protected chip is expected) -> Recovering (CTRL-AP ERASEALL,
//          reconnect) -> [nRF91: program + verify the disable-AP-protect image] -> reset(Go).
// Erase (code only): the PIB is read before and after; a change fails with target.erase_failed
//          (review R5).
// Reset:   Connecting (identify, family check; protected allowed) -> Resetting.
// ReadMemory: Connecting (identify, family check) -> read (review R7).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/programmer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hio::device::detail {

struct WorkflowContext {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr;
    jlink::IProgrammer* programmer = nullptr;
    firmware::ProgressFn progress; // phase reports
    std::stop_token stop;
    std::function<void(const std::string&)> log; // console file "@" line + hio.flash
    // Connects the target again with this J-Link device name (LTE: the exact part's name).
    std::function<Status(std::string_view jlinkDevice)> connectAs;
    // The J-Link device name the probe is connected with now.
    std::string connectedDevice;
    // Set by the workflows: the CPU is halted now (reset(Halt), erase); cleared by reset(Go).
    bool cpuHalted = false;
};

// The image split into the bytes for the code flash and the target's UICR records.
struct ImagePlan {
    firmware::FirmwareImage code;
    std::optional<firmware::FirmwareImage> uicr;
};

// Checks the image against the target (ADR-0035, ADR-0036). Errors: firmware.out_of_range (a byte
// outside flash and UICR, or inside the PIB), firmware.uicr_in_image (UICR bytes without
// allowUicr), usage.invalid_argument (empty image).
[[nodiscard]] Result<ImagePlan> planImage(const DeviceProfile& profile, const TargetProfile& target,
                                          const firmware::FirmwareImage& image, bool allowUicr);

// identify -> family check -> protection check (unless `allowProtected`); on the LTE MCU it
// reconnects with the exact part's J-Link device name. The returned ChipInfo has its name.
// Errors: target.wrong_family, target.protected, those of identify().
[[nodiscard]] Result<jlink::ChipInfo> identifyTarget(WorkflowContext& context, bool allowProtected);

[[nodiscard]] Result<jlink::ChipInfo>
runFlash(WorkflowContext& context, const firmware::FirmwareImage& image, FlashOptions options);
[[nodiscard]] Result<jlink::ChipInfo> runErase(WorkflowContext& context, EraseScope scope);
// `reconnect` connects the target after the erase-all (the CTRL-AP reset drops the connection).
[[nodiscard]] Result<std::optional<jlink::ChipInfo>>
runRecover(WorkflowContext& context, const std::function<Status()>& reconnect);
[[nodiscard]] Status runReset(WorkflowContext& context, ResetMode mode);
[[nodiscard]] Result<std::vector<std::byte>> runReadMemory(WorkflowContext& context,
                                                           std::uint32_t address, std::size_t size);

// The embedded nrf91_disable_ap_protect.hex (ADR-0039), parsed.
[[nodiscard]] Result<firmware::FirmwareImage> recoverImage(RecoverImage image);

// Adds the target to the message (and suggestion) of the M2 target / firmware errors:
// "Programming the lte MCU failed.", "Expected the app MCU (nRF52840) but found nRF9151.", ...
[[nodiscard]] Error forTarget(Error error, const DeviceProfile& profile,
                              const TargetProfile& target);

} // namespace hio::device::detail

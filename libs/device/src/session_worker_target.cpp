#include "session_worker_target.h"

#include "flash_workflows.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/firmware/firmware_image.h"
#include "hio/hw/jlink/programmer.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hio::device::detail {

namespace {

void log(const WorkflowContext& context, const std::string& text) {
    if (context.log) {
        context.log(text);
    }
}

// The family check (review R7) before any access. A protected chip is an error here
// (target.protected), as for a memory read.
Status identified(WorkflowContext& context) {
    if (auto chip = identifyTarget(context, false); !chip) {
        return std::unexpected(std::move(chip).error());
    }
    return {};
}

TargetStatus haltedStatus(const WorkflowContext& context, bool halted) {
    return TargetStatus{
        .halted = halted, .coreId = std::nullopt, .deviceName = context.connectedDevice};
}

} // namespace

Result<TargetStatus> runHalt(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    log(context, "Halt");
    if (Status halted = context.programmer->halt(); !halted) {
        return std::unexpected(std::move(halted).error());
    }
    return haltedStatus(context, true);
}

Result<TargetStatus> runGo(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    log(context, "Go");
    if (Status running = context.programmer->go(); !running) {
        return std::unexpected(std::move(running).error());
    }
    return haltedStatus(context, false);
}

Result<TargetStatus> runTargetStatus(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    auto halted = context.programmer->isHalted();
    if (!halted) {
        return std::unexpected(std::move(halted).error());
    }
    // Review S6: without JLINKARM_GetId the status is still reported, without a core id.
    TargetStatus status = haltedStatus(context, *halted);
    if (auto coreId = context.programmer->coreId(); coreId) {
        status.coreId = *coreId;
    } else if (coreId.error().code == ErrorCode::DependencyJLinkFunctionMissing) {
        log(context, "Core id unknown (" + coreId.error().detail + " missing)");
    } else {
        return std::unexpected(std::move(coreId).error());
    }
    return status;
}

Status runWriteMemory(WorkflowContext& context, std::uint32_t address,
                      std::span<const std::byte> data, AccessWidth width) {
    // ADR-0160: checked again here (DeviceSession checks at request time), before any access.
    if (context.target == nullptr) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no target"));
    }
    if (Status allowed = checkMemoryWrite(*context.target, address, data.size()); !allowed) {
        return allowed;
    }
    if (Status ready = identified(context); !ready) {
        return ready;
    }
    log(context, std::format("Writing {} bytes at 0x{:08X} ({}-bit units)", data.size(), address,
                             jlink::unitBytes(width) * 8));
    return context.programmer->writeMemory(address, data, width);
}

Result<std::vector<RegisterValue>> runReadRegisters(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    return context.programmer->readRegisters();
}

Result<std::vector<MemoryZone>> runMemoryZones(WorkflowContext& context) {
    if (Status ready = identified(context); !ready) {
        return std::unexpected(std::move(ready).error());
    }
    return context.programmer->memoryZones();
}

Result<jlink::ChipInfo> runWriteFlash(WorkflowContext& context,
                                      const firmware::FirmwareImage& image, bool allowUicr) {
    return runFlash(context, image, FlashOptions{.halt = false, .allowUicr = allowUicr});
}

} // namespace hio::device::detail

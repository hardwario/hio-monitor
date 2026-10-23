#pragma once

// Low-level target operations of the session worker (plan/features/0013, ADR-0099, ADR-0158):
// halt, go, targetStatus, writeMemory, readRegisters, memoryZones and writeFlash. Pure logic over
// jlink::IProgrammer and the WorkflowContext of flash_workflows.h (the same pattern), run on the
// session worker (dispatched from executeOperation() in session_worker.cpp) and unit-tested with
// a GMock programmer. readMemory with a width is runReadMemory() in flash_workflows.h.
//
// Every operation first identifies the target (family check, review R7: the wrong connector never
// touches the other MCU) and then calls the programmer. None of them changes
// WorkflowContext::cpuHalted: whether RTT stays attached is decided by the worker (ADR-0157).

#include "flash_workflows.h"
#include "hio/core/result.h"
#include "hio/device/operation.h"
#include "hio/firmware/firmware_image.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hio::device::detail {

// Halt / go: the result's `halted` is true / false; deviceName is the connected J-Link device.
[[nodiscard]] Result<TargetStatus> runHalt(WorkflowContext& context);
[[nodiscard]] Result<TargetStatus> runGo(WorkflowContext& context);
// Halted state (JLINKARM_IsHalted), core id (JLINKARM_GetId; nullopt when the library lacks it,
// review S6) and the J-Link device name. Errors: dependency.jlink_function_missing without
// JLINKARM_IsHalted, those of IProgrammer::isHalted.
[[nodiscard]] Result<TargetStatus> runTargetStatus(WorkflowContext& context);
// Errors: target.address_out_of_range (checkMemoryWrite(), ADR-0160), those of
// IProgrammer::writeMemory.
[[nodiscard]] Status runWriteMemory(WorkflowContext& context, std::uint32_t address,
                                    std::span<const std::byte> data, AccessWidth width);
// Errors: target.not_halted, those of IProgrammer::readRegisters.
[[nodiscard]] Result<std::vector<RegisterValue>> runReadRegisters(WorkflowContext& context);
[[nodiscard]] Result<std::vector<MemoryZone>> runMemoryZones(WorkflowContext& context);
// runFlash() of the raw bytes (one segment) with the CPU running afterwards: reset + halt,
// program, verify, reset (go). Range rules of planImage() (M2): outside flash / UICR or inside
// the PIB -> firmware.out_of_range; UICR without `allowUicr` -> firmware.uicr_in_image.
[[nodiscard]] Result<jlink::ChipInfo>
runWriteFlash(WorkflowContext& context, const firmware::FirmwareImage& image, bool allowUicr);

} // namespace hio::device::detail

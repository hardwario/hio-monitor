#include "flash_workflows.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/programmer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::device::detail {

namespace {

using firmware::AddressRange;
using firmware::FirmwareImage;
using firmware::ProgramPhase;
using firmware::Segment;

QString hexAddress(std::uint64_t address) {
    return QStringLiteral("0x%1").arg(address, 8, 16, QLatin1Char('0'));
}

QString partName(const DeviceProfile& profile, std::uint32_t part) {
    for (const TargetProfile& target : profile.targets) {
        if (const ChipPart* known = findPart(target, part)) {
            return toQString(known->displayName);
        }
    }
    return QCoreApplication::translate("hio::device", "an unknown MCU (part %1)")
        .arg(QStringLiteral("0x%1").arg(part, 0, 16));
}

void report(const WorkflowContext& context, ProgramPhase phase) {
    if (context.progress) {
        context.progress({.phase = phase, .done = 0, .total = 0});
    }
}

void log(const WorkflowContext& context, const std::string& text) {
    if (context.log) {
        context.log(text);
    }
}

std::string hexBytes(std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        text += digits[std::to_integer<unsigned>(b) >> 4U];
        text += digits[std::to_integer<unsigned>(b) & 0x0FU];
    }
    return text;
}

// The UICR page with the image's UICR bytes merged in (ADR-0036).
Result<FirmwareImage> mergedUicr(WorkflowContext& context, const FirmwareImage& uicr) {
    const TargetProfile& target = *context.target;
    auto device = context.programmer->readMemory(static_cast<std::uint32_t>(target.uicr.start),
                                                 static_cast<std::size_t>(target.uicr.size()));
    if (!device) {
        return std::unexpected(std::move(device).error());
    }
    std::vector<std::byte> page = std::move(*device);
    for (const Segment& segment : uicr.segments()) {
        const std::size_t offset = segment.address - target.uicr.start;
        std::ranges::copy(segment.data, page.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    std::vector<Segment> segments;
    segments.push_back(
        Segment{.address = static_cast<std::uint32_t>(target.uicr.start), .data = std::move(page)});
    auto merged = FirmwareImage::fromSegments(std::move(segments));
    if (!merged) {
        return merged;
    }
    if (!target.pibErasableWithUicr) {
        // The PIB is not erased with the UICR (nRF91 OTP area): it is never written.
        if (auto pib = merged->splitOut(pibRange(target)); !pib) {
            return std::unexpected(std::move(pib).error());
        }
    }
    return merged;
}

Result<FirmwareImage> combine(const FirmwareImage& code, const std::optional<FirmwareImage>& uicr) {
    std::vector<Segment> segments = code.segments();
    if (uicr) {
        segments.insert(segments.end(), uicr->segments().begin(), uicr->segments().end());
    }
    return FirmwareImage::fromSegments(std::move(segments), code.startAddress());
}

} // namespace

Error forTarget(Error error, const DeviceProfile& profile, const TargetProfile& target) {
    const QString id = toQString(target.id);
    switch (error.code) {
    case ErrorCode::TargetProgramFailed:
        error.message = QCoreApplication::translate("hio::device", "Programming the %1 MCU failed.")
                            .arg(id)
                            .toStdString();
        break;
    case ErrorCode::TargetEraseFailed:
        error.message = QCoreApplication::translate("hio::device", "Erasing the %1 MCU failed.")
                            .arg(id)
                            .toStdString();
        break;
    case ErrorCode::TargetRecoverFailed:
        error.message = QCoreApplication::translate("hio::device", "Recovering the %1 MCU failed.")
                            .arg(id)
                            .toStdString();
        break;
    case ErrorCode::TargetProtected:
        error.message =
            QCoreApplication::translate("hio::device", "The %1 MCU is read-back protected.")
                .arg(id)
                .toStdString();
        error.suggestion =
            QCoreApplication::translate("hio::device", "Run `hio %1 %2 recover` (erases the MCU).")
                .arg(toQString(profile.id), id)
                .toStdString();
        break;
    default:
        break;
    }
    return error;
}

Result<ImagePlan> planImage(const DeviceProfile& profile, const TargetProfile& target,
                            const FirmwareImage& image, bool allowUicr) {
    (void)profile;
    if (image.empty()) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, "the image has no data");
        error.message = QCoreApplication::translate("hio::device", "The firmware image is empty.")
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    const AddressRange pib = pibRange(target);
    for (const Segment& segment : image.segments()) {
        const AddressRange range = segment.range();
        if (range.overlaps(pib)) {
            Error error = makeError(ErrorCode::FirmwareOutOfRange,
                                    std::format("segment 0x{:08X}-0x{:08X} overlaps the PIB "
                                                "0x{:08X}-0x{:08X}",
                                                range.start, range.end, pib.start, pib.end));
            error.message =
                QCoreApplication::translate(
                    "hio::device",
                    "The firmware writes into the Product Information Block (address %1).")
                    .arg(hexAddress(std::max(range.start, pib.start)))
                    .toStdString();
            return std::unexpected(std::move(error));
        }
        if (!target.flash.contains(range) && !target.uicr.contains(range)) {
            const std::uint64_t outside =
                target.flash.contains(range.start) ? target.flash.end : range.start;
            Error error =
                makeError(ErrorCode::FirmwareOutOfRange,
                          std::format("segment 0x{:08X}-0x{:08X}; flash 0x{:08X}-0x{:08X}, "
                                      "UICR 0x{:08X}-0x{:08X}",
                                      range.start, range.end, target.flash.start, target.flash.end,
                                      target.uicr.start, target.uicr.end));
            error.message = QCoreApplication::translate(
                                "hio::device", "The firmware does not fit the %1 MCU (address %2).")
                                .arg(toQString(target.id), hexAddress(outside))
                                .toStdString();
            error.suggestion = QCoreApplication::translate(
                                   "hio::device", "Check that the firmware is for the %1 MCU.")
                                   .arg(toQString(target.id))
                                   .toStdString();
            return std::unexpected(std::move(error));
        }
    }
    ImagePlan plan{.code = image, .uicr = std::nullopt};
    if (image.intersects(target.uicr)) {
        if (!allowUicr) {
            return std::unexpected(makeError(
                ErrorCode::FirmwareUicrInImage,
                std::format("UICR 0x{:08X}-0x{:08X}", target.uicr.start, target.uicr.end)));
        }
        auto uicr = plan.code.splitOut(target.uicr);
        if (!uicr) {
            return std::unexpected(std::move(uicr).error());
        }
        plan.uicr = std::move(*uicr);
    }
    return plan;
}

namespace {

Error wrongFamily(const TargetProfile& target, const QString& found, std::string detail) {
    const QString expected =
        toQString(target.parts.empty() ? std::string_view() : target.parts.front().displayName);
    Error error = makeError(ErrorCode::TargetWrongFamily, std::move(detail));
    error.message =
        QCoreApplication::translate("hio::device", "Expected the %1 MCU (%2) but found %3.")
            .arg(toQString(target.id), expected, found)
            .toStdString();
    error.suggestion =
        QCoreApplication::translate("hio::device", "Connect the J-Link to the %1 connector.")
            .arg(toQString(target.id))
            .toStdString();
    return error;
}

// FICR could not be read; the family comes from the CTRL-AP IDR (review R2).
Result<jlink::ChipInfo> unreadableChip(WorkflowContext& context, jlink::ChipInfo chip,
                                       bool allowProtected) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    if (chip.family != target.chipFamily.name) {
        return std::unexpected(wrongFamily(
            target,
            QCoreApplication::translate("hio::device", "an %1 MCU").arg(toQString(chip.family)),
            std::format("Nordic CTRL-AP at the {} AP index", chip.family)));
    }
    if (chip.protectedChip) {
        log(context, std::format("{} MCU: APPROTECT enabled", target.id));
        if (allowProtected) {
            return chip;
        }
        return std::unexpected(forTarget(makeError(ErrorCode::TargetProtected), profile, target));
    }
    if (allowProtected) {
        log(context, std::format("{} MCU: FICR unreadable, its CTRL-AP answers", target.id));
        return chip; // recover: the expected CTRL-AP is there
    }
    Error error =
        makeError(ErrorCode::TargetConnectFailed, "FICR unreadable, APPROTECT not enabled");
    error.message =
        QCoreApplication::translate("hio::device", "The type of the %1 MCU could not be read.")
            .arg(toQString(target.id))
            .toStdString();
    return std::unexpected(std::move(error));
}

} // namespace

Result<jlink::ChipInfo> identifyTarget(WorkflowContext& context, bool allowProtected) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    report(context, ProgramPhase::Connecting);
    auto chip = context.programmer->identify();
    if (!chip) {
        return std::unexpected(forTarget(std::move(chip).error(), profile, target));
    }
    if (chip->part == 0) {
        return unreadableChip(context, std::move(*chip), allowProtected);
    }
    const ChipPart* part = findPart(target, chip->part);
    if (part == nullptr) {
        return std::unexpected(wrongFamily(target, partName(profile, chip->part),
                                           std::format("FICR part 0x{:X}", chip->part)));
    }
    chip->name = std::string(part->displayName);
    // LTE: connected with the first part's J-Link name; use the exact part's name when it differs
    // (to confirm in Phase 0).
    if (context.connectAs && !context.connectedDevice.empty() &&
        part->jlinkDevice != context.connectedDevice) {
        log(context, std::format("Reconnecting as {}", part->jlinkDevice));
        if (Status reconnected = context.connectAs(part->jlinkDevice); !reconnected) {
            return std::unexpected(std::move(reconnected).error());
        }
        context.connectedDevice = std::string(part->jlinkDevice);
    }
    log(context, std::format("Target: {} ({})", chip->name, target.id));
    return chip;
}

Result<jlink::ChipInfo> runFlash(WorkflowContext& context, const FirmwareImage& image,
                                 FlashOptions options) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    auto plan = planImage(profile, target, image, options.allowUicr);
    if (!plan) {
        return std::unexpected(std::move(plan).error());
    }
    auto chip = identifyTarget(context, false);
    if (!chip) {
        return chip;
    }
    if (Status halted = context.programmer->reset(jlink::ResetMode::Halt); !halted) {
        return std::unexpected(forTarget(std::move(halted).error(), profile, target));
    }
    context.cpuHalted = true;
    std::optional<FirmwareImage> uicr;
    if (plan->uicr) {
        auto merged = mergedUicr(context, *plan->uicr);
        if (!merged) {
            return std::unexpected(std::move(merged).error());
        }
        uicr = std::move(*merged);
        log(context, "UICR records merged (the Product Information Block is kept)");
    }
    auto written = combine(plan->code, uicr);
    if (!written) {
        return std::unexpected(std::move(written).error());
    }
    log(context,
        std::format("Programming {} bytes (SHA-256 {})", image.totalBytes(), image.sha256()));
    if (Status programmed = context.programmer->program(*written, context.progress, context.stop);
        !programmed) {
        return std::unexpected(forTarget(std::move(programmed).error(), profile, target));
    }
    log(context, "Verifying");
    if (Status verified = context.programmer->verify(*written, context.progress, context.stop);
        !verified) {
        return std::unexpected(forTarget(std::move(verified).error(), profile, target));
    }
    report(context, ProgramPhase::Resetting);
    const jlink::ResetMode mode = options.halt ? jlink::ResetMode::Halt : jlink::ResetMode::Go;
    log(context, options.halt ? "Resetting (halt)" : "Resetting (go)");
    if (Status reset = context.programmer->reset(mode); !reset) {
        return std::unexpected(forTarget(std::move(reset).error(), profile, target));
    }
    context.cpuHalted = options.halt;
    log(context, "Flashing completed");
    return chip;
}

Result<jlink::ChipInfo> runErase(WorkflowContext& context, EraseScope scope) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    auto chip = identifyTarget(context, false);
    if (!chip) {
        return chip;
    }
    report(context, ProgramPhase::Erasing);
    context.cpuHalted = true; // the erase halts the CPU; nothing runs afterwards
    Status erased;
    if (scope == EraseScope::Code) {
        // Review R5: the PIB must survive a code-only erase. Phase 0.3 confirms the loader keeps
        // UICR; until then (and afterwards, as a guard) the PIB is compared before and after.
        const firmware::AddressRange pib = pibRange(target);
        const auto pibAddress = static_cast<std::uint32_t>(pib.start);
        auto before = context.programmer->readMemory(pibAddress, pibSize);
        if (!before) {
            return std::unexpected(forTarget(std::move(before).error(), profile, target));
        }
        log(context, "Erasing the code flash (UICR and the Product Information Block are kept)");
        erased = context.programmer->eraseCode(target.flash, context.progress, context.stop);
        if (erased) {
            auto after = context.programmer->readMemory(pibAddress, pibSize);
            if (!after) {
                // Review R17: say that the PIB state is unknown; keep its old bytes in the log.
                log(context, "ERROR: the Product Information Block could not be read after the "
                             "code-only erase; it was " +
                                 hexBytes(*before));
                Error error =
                    makeError(ErrorCode::TargetEraseFailed,
                              std::format("PIB at 0x{:08X} not readable after the erase: "
                                          "{}; before: {}",
                                          pib.start, after.error().detail, hexBytes(*before)));
                error.message =
                    QCoreApplication::translate("hio::device",
                                                "The Product Information Block of the %1 MCU could "
                                                "not be checked after the erase.")
                        .arg(toQString(target.id))
                        .toStdString();
                // Review R19: a second erase would read the possibly wiped PIB as "before".
                error.suggestion =
                    QCoreApplication::translate(
                        "hio::device",
                        "Do not erase again. Run `hio %1 %2 pib read` and compare it with the "
                        "bytes "
                        "in the log (see Details); if they differ, contact HARDWARIO support.")
                        .arg(toQString(profile.id), toQString(target.id))
                        .toStdString();
                return std::unexpected(std::move(error));
            }
            if (*after != *before) {
                log(context, "ERROR: the code-only erase changed the Product Information Block");
                Error error = makeError(ErrorCode::TargetEraseFailed,
                                        std::format("PIB at 0x{:08X} changed by the code-only "
                                                    "erase",
                                                    pib.start));
                error.message = QCoreApplication::translate(
                                    "hio::device", "Erasing the code flash of the %1 MCU also "
                                                   "changed its Product Information Block.")
                                    .arg(toQString(target.id))
                                    .toStdString();
                error.suggestion =
                    QCoreApplication::translate(
                        "hio::device",
                        "Do not erase again; report this with the log file to HARDWARIO support.")
                        .toStdString();
                return std::unexpected(std::move(error));
            }
        }
    } else {
        log(context, "Erasing all flash and UICR");
        erased = context.programmer->eraseAll();
    }
    if (!erased) {
        return std::unexpected(forTarget(std::move(erased).error(), profile, target));
    }
    log(context, "Erase completed");
    return chip;
}

Result<std::optional<jlink::ChipInfo>> runRecover(WorkflowContext& context,
                                                  const std::function<Status()>& reconnect) {
    const DeviceProfile& profile = *context.profile;
    const TargetProfile& target = *context.target;
    // A protected chip is what recover is for. Only the expected family's CTRL-AP is recovered
    // (its FICR or its CTRL-AP IDR answered; review R2).
    auto before = identifyTarget(context, true);
    if (!before) {
        return std::unexpected(std::move(before).error());
    }
    report(context, ProgramPhase::Recovering);
    log(context, "Recovering (CTRL-AP ERASEALL)");
    context.cpuHalted = true;
    if (Status recovered = context.programmer->recover(context.stop); !recovered) {
        return std::unexpected(forTarget(std::move(recovered).error(), profile, target));
    }
    if (reconnect) {
        if (Status connected = reconnect(); !connected) {
            Error error = makeError(ErrorCode::TargetRecoverFailed, connected.error().detail);
            return std::unexpected(forTarget(std::move(error), profile, target));
        }
    }
    std::optional<jlink::ChipInfo> chip;
    if (auto after = context.programmer->identify(); after && !after->protectedChip) {
        if (const ChipPart* part = findPart(target, after->part)) {
            after->name = std::string(part->displayName);
        }
        chip = std::move(*after);
    }
    if (target.recoverImage == RecoverImage::Nrf91DisableApProtect) {
        auto image = recoverImage(target.recoverImage);
        if (!image) {
            return std::unexpected(std::move(image).error());
        }
        log(context, "Programming the debug-port unlock image");
        if (Status programmed = context.programmer->program(*image, context.progress, {});
            !programmed) {
            return std::unexpected(forTarget(std::move(programmed).error(), profile, target));
        }
        if (Status verified = context.programmer->verify(*image, context.progress, {}); !verified) {
            return std::unexpected(forTarget(std::move(verified).error(), profile, target));
        }
    }
    report(context, ProgramPhase::Resetting);
    if (Status reset = context.programmer->reset(jlink::ResetMode::Go); !reset) {
        return std::unexpected(forTarget(std::move(reset).error(), profile, target));
    }
    context.cpuHalted = false;
    log(context, "Recover completed");
    return chip;
}

Status runReset(WorkflowContext& context, ResetMode mode) {
    // Review R7: the family check first, so the wrong connector never resets the other MCU.
    if (auto chip = identifyTarget(context, true); !chip) {
        return std::unexpected(std::move(chip).error());
    }
    report(context, ProgramPhase::Resetting);
    log(context, mode == ResetMode::Halt ? "Reset (halt)" : "Reset (go)");
    if (Status reset = context.programmer->reset(mode); !reset) {
        return std::unexpected(
            forTarget(std::move(reset).error(), *context.profile, *context.target));
    }
    context.cpuHalted = mode == ResetMode::Halt;
    return {};
}

Result<std::vector<std::byte>> runReadMemory(WorkflowContext& context, std::uint32_t address,
                                             std::size_t size) {
    // Review R7: the family check first (pib read / uicr read on the wrong connector).
    if (auto chip = identifyTarget(context, false); !chip) {
        return std::unexpected(std::move(chip).error());
    }
    return context.programmer->readMemory(address, size);
}

Result<FirmwareImage> recoverImage(RecoverImage image) {
    if (image != RecoverImage::Nrf91DisableApProtect) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no recover image"));
    }
    QFile file(QStringLiteral(":/hio/device/nrf91_disable_ap_protect.hex"));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected,
                                         "embedded nrf91_disable_ap_protect.hex missing"));
    }
    const QByteArray text = file.readAll();
    return firmware::parseIntelHex(
        std::as_bytes(std::span(text.constData(), static_cast<std::size_t>(text.size()))),
        "nrf91_disable_ap_protect.hex");
}

} // namespace hio::device::detail

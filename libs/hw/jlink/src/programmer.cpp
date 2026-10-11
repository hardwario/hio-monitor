#include "hio/hw/jlink/programmer.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"

#include <QChar>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::jlink {

namespace {

using firmware::FirmwareImage;
using firmware::ProgramPhase;
using firmware::ProgramProgress;
using firmware::ProgressFn;
using firmware::Segment;

void report(const ProgressFn& progress, ProgramPhase phase, std::uint64_t done,
            std::uint64_t total) {
    if (progress) {
        progress(ProgramProgress{.phase = phase, .done = done, .total = total});
    }
}

bool containsIgnoringCase(std::string_view text, std::string_view word) {
    return std::ranges::search(text, word, [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           }).begin() != text.end();
}

Error timedOut(std::string detail) { return makeError(ErrorCode::Timeout, std::move(detail)); }

} // namespace

JLinkProgrammer::JLinkProgrammer(IJLinkApi& api, JLinkProbe& probe, Clock& clock,
                                 ProgrammerConfig config)
    : m_api(api), m_probe(probe), m_clock(clock), m_config(std::move(config)) {}

std::string JLinkProgrammer::libraryDetail(std::string detail) {
    const std::string libraryText = m_api.takeErrorText();
    if (!libraryText.empty()) {
        detail += "; J-Link: " + libraryText;
    }
    return detail;
}

int JLinkProgrammer::selectCtrlAp(const ChipFamily& family) {
    if (const int configured = m_api.coresightConfigure(); configured < 0) {
        return configured;
    }
    return m_api.coresightWrite(dpSelect, false, std::uint32_t{family.ctrlApIndex} << 24U);
}

std::optional<bool> JLinkProgrammer::isProtected(const ChipFamily& family) {
    if (selectCtrlAp(family) < 0) {
        return std::nullopt;
    }
    const auto status = m_api.coresightRead(ctrlApApProtectStatus, true);
    if (!status) {
        return std::nullopt;
    }
    return (*status & 1U) == 0;
}

Result<ChipInfo> JLinkProgrammer::identify() {
    std::string tried;
    for (const ChipFamily& family : m_config.families) {
        const auto part = m_api.readU32(family.ficrPart);
        if (part && *part != 0 && *part != 0xFFFFFFFFU) {
            const auto variant = m_api.readU32(family.ficrVariant);
            ChipInfo info{
                .part = *part, .variant = variant.value_or(0), .protectedChip = false, .name = {}};
            qCInfo(log::lcFlash).noquote() << QStringLiteral("FICR (%1): part 0x%2 variant 0x%3")
                                                  .arg(toQString(family.name))
                                                  .arg(info.part, 0, 16)
                                                  .arg(info.variant, 8, 16, QLatin1Char('0'));
            return info;
        }
        tried += std::format(
            "{}{} FICR 0x{:08X}: {}", tried.empty() ? "" : "; ", family.name, family.ficrPart,
            part ? std::format("0x{:08X}", *part) : std::format("read error {}", part.error()));
    }
    // FICR unreadable: a protected chip blocks the memory access port (APPROTECT).
    for (const ChipFamily& family : m_config.families) {
        if (const auto protectedChip = isProtected(family); protectedChip && *protectedChip) {
            qCInfo(log::lcFlash).noquote()
                << "CTRL-AP" << family.ctrlApIndex << "reports APPROTECT enabled";
            return ChipInfo{.part = 0, .variant = 0, .protectedChip = true, .name = {}};
        }
    }
    Error error = makeError(ErrorCode::TargetConnectFailed, libraryDetail(tried));
    error.message =
        QCoreApplication::translate("hio::jlink", "The MCU type could not be read (FICR).")
            .toStdString();
    return std::unexpected(std::move(error));
}

Status JLinkProgrammer::download(const std::vector<Segment>& segments, bool programsData,
                                 ErrorCode failure, const ProgressFn& progress,
                                 const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    std::uint64_t total = 0;
    for (const Segment& segment : segments) {
        total += segment.data.size();
    }
    const auto deadline = m_clock.now() + m_config.timing.programTimeout;
    report(progress, ProgramPhase::Erasing, 0, 0);
    m_api.beginDownload();
    for (const Segment& segment : segments) {
        const std::span<const std::byte> data(segment.data);
        for (std::size_t offset = 0; offset < data.size(); offset += m_config.timing.writeChunk) {
            if (m_clock.now() > deadline) {
                // The buffered data is handed to the loader only by EndDownload, which is not
                // called: nothing was written.
                return std::unexpected(timedOut("flash download buffering exceeded the deadline"));
            }
            const auto chunk =
                data.subspan(offset, std::min(m_config.timing.writeChunk, data.size() - offset));
            const auto address = static_cast<std::uint32_t>(segment.address + offset);
            const int written = m_api.writeMemory(address, chunk);
            if (written < 0) {
                (void)m_api.endDownload({}); // close the download mode of the library
                return std::unexpected(makeError(
                    failure,
                    libraryDetail(std::format("JLINKARM_WriteMem(0x{:08X}, {}) returned {}",
                                              address, chunk.size(), written))));
            }
        }
    }
    // The loader's own phases: when it programs data, "Erase" -> Erasing and everything else ->
    // Programming, never going back to Erasing; an erase reports everything as Erasing.
    ProgramPhase current = ProgramPhase::Erasing;
    const FlashProgressFn loaderProgress = [&](std::string_view action, int percent) {
        if (programsData && !containsIgnoringCase(action, "erase")) {
            current = ProgramPhase::Programming;
        }
        const auto clamped = static_cast<std::uint64_t>(std::clamp(percent, 0, 100));
        report(progress, current, total * clamped / 100, total);
    };
    const int result = m_api.endDownload(loaderProgress);
    m_api.flushMessages();
    if (result < 0) {
        return std::unexpected(makeError(
            failure, libraryDetail(std::format("JLINKARM_EndDownload returned {}", result))));
    }
    if (m_clock.now() > deadline) {
        return std::unexpected(timedOut("flash download exceeded the deadline"));
    }
    return {};
}

Status JLinkProgrammer::program(const FirmwareImage& image, const ProgressFn& progress,
                                std::stop_token stop) {
    if (auto status =
            download(image.segments(), true, ErrorCode::TargetProgramFailed, progress, stop);
        !status) {
        return status;
    }
    report(progress, ProgramPhase::Programming, image.totalBytes(), image.totalBytes());
    qCInfo(log::lcFlash) << "Programmed" << image.totalBytes() << "bytes in"
                         << image.segments().size() << "segment(s)";
    return {};
}

Status JLinkProgrammer::verify(const FirmwareImage& image, const ProgressFn& progress,
                               std::stop_token stop) {
    const std::uint64_t total = image.totalBytes();
    const auto deadline = m_clock.now() + m_config.timing.verifyTimeout;
    std::vector<std::byte> buffer(m_config.timing.verifyChunk);
    std::uint64_t done = 0;
    report(progress, ProgramPhase::Verifying, 0, total);
    for (const Segment& segment : image.segments()) {
        const std::span<const std::byte> expected(segment.data);
        for (std::size_t offset = 0; offset < expected.size(); offset += buffer.size()) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::Cancelled);
            }
            if (m_clock.now() > deadline) {
                return std::unexpected(timedOut("verify exceeded the deadline"));
            }
            const auto want =
                expected.subspan(offset, std::min(buffer.size(), expected.size() - offset));
            const auto address = static_cast<std::uint32_t>(segment.address + offset);
            const auto read = std::span(buffer).first(want.size());
            const int count = m_api.readMemory(address, read);
            if (count < 0 || static_cast<std::size_t>(count) < want.size()) {
                return std::unexpected(makeError(
                    ErrorCode::TargetConnectFailed,
                    libraryDetail(std::format("JLINKARM_ReadMemEx(0x{:08X}, {}) returned {}",
                                              address, want.size(), count))));
            }
            const auto mismatch = std::ranges::mismatch(want, read);
            if (mismatch.in1 != want.end()) {
                const auto index = static_cast<std::size_t>(mismatch.in1 - want.begin());
                const std::uint32_t at = address + static_cast<std::uint32_t>(index);
                Error error = makeError(ErrorCode::FirmwareVerifyFailed,
                                        std::format("0x{:08X}: expected 0x{:02X}, read 0x{:02X}",
                                                    at, static_cast<unsigned>(*mismatch.in1),
                                                    static_cast<unsigned>(*mismatch.in2)));
                error.message =
                    QCoreApplication::translate("hio::jlink", "Verification failed at address %1.")
                        .arg(QStringLiteral("0x%1").arg(at, 8, 16, QLatin1Char('0')))
                        .toStdString();
                return std::unexpected(std::move(error));
            }
            done += want.size();
            report(progress, ProgramPhase::Verifying, done, total);
        }
    }
    qCInfo(log::lcFlash) << "Verified" << total << "bytes";
    return {};
}

Status JLinkProgrammer::eraseCode(firmware::AddressRange flash, const ProgressFn& progress,
                                  std::stop_token stop) {
    // To confirm in Phase 0 (plan 0003, 0.3): 0xFF over the whole range through the flash loader,
    // which erases every sector whose content differs; the UICR is outside the range.
    std::vector<Segment> segments;
    constexpr std::uint64_t piece = 64ULL * 1024;
    for (std::uint64_t address = flash.start; address < flash.end; address += piece) {
        const std::uint64_t size = std::min(piece, flash.end - address);
        segments.push_back(Segment{.address = static_cast<std::uint32_t>(address),
                                   .data = std::vector<std::byte>(size, std::byte{0xFF})});
    }
    if (auto status = download(segments, false, ErrorCode::TargetEraseFailed, progress, stop);
        !status) {
        return status;
    }
    report(progress, ProgramPhase::Erasing, flash.size(), flash.size());
    qCInfo(log::lcFlash).noquote() << QStringLiteral("Erased the code flash 0x%1-0x%2")
                                          .arg(flash.start, 8, 16, QLatin1Char('0'))
                                          .arg(flash.end, 8, 16, QLatin1Char('0'));
    return {};
}

Status JLinkProgrammer::eraseAll() {
    (void)m_api.halt(); // pylink erase(): halt first, ignore a failure
    const auto started = m_clock.now();
    const int result = m_api.eraseChip();
    m_api.flushMessages();
    if (result < 0) {
        return std::unexpected(
            makeError(ErrorCode::TargetEraseFailed,
                      libraryDetail(std::format("JLINK_EraseChip returned {}", result))));
    }
    if (m_clock.now() - started > m_config.timing.eraseAllTimeout) {
        return std::unexpected(timedOut("JLINK_EraseChip exceeded the deadline"));
    }
    qCInfo(log::lcFlash) << "Erased the chip (flash and UICR)";
    return {};
}

Status JLinkProgrammer::recover(std::stop_token stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    if (m_config.families.empty()) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no chip family"));
    }
    const ChipFamily& family = m_config.families.front();
    const auto failed = [this](std::string detail) {
        return std::unexpected(
            makeError(ErrorCode::TargetRecoverFailed, libraryDetail(std::move(detail))));
    };
    if (const int selected = selectCtrlAp(family); selected < 0) {
        return failed(
            std::format("selecting CTRL-AP {} returned {}", family.ctrlApIndex, selected));
    }
    qCInfo(log::lcFlash) << "Recover: CTRL-AP" << family.ctrlApIndex << "ERASEALL";
    if (const int written = m_api.coresightWrite(ctrlApEraseAll, true, 1); written < 0) {
        return failed(std::format("CTRL-AP ERASEALL write returned {}", written));
    }
    // Never interrupted (plan: no cancel in the middle of a recover).
    const auto deadline = m_clock.now() + m_config.timing.recoverTimeout;
    while (true) {
        const auto status = m_api.coresightRead(ctrlApEraseAllStatus, true);
        if (!status) {
            return failed(std::format("CTRL-AP ERASEALLSTATUS read returned {}", status.error()));
        }
        if (*status == 0) {
            break;
        }
        if (m_clock.now() >= deadline) {
            return std::unexpected(makeError(ErrorCode::TargetRecoverFailed,
                                             libraryDetail("ERASEALL did not finish within 60 s")));
        }
        (void)m_clock.sleepFor(m_config.timing.recoverPoll, std::stop_token{});
    }
    // CTRL-AP RESET toggle: a system reset that ends the erase-all state.
    if (m_api.coresightWrite(ctrlApReset, true, 1) < 0 ||
        m_api.coresightWrite(ctrlApReset, true, 0) < 0 ||
        m_api.coresightWrite(ctrlApEraseAll, true, 0) < 0) {
        return failed("CTRL-AP RESET toggle failed");
    }
    m_api.flushMessages();
    qCInfo(log::lcFlash) << "Recover: ERASEALL done";
    return {};
}

Result<std::vector<std::byte>> JLinkProgrammer::readMemory(std::uint32_t address,
                                                           std::size_t size) {
    if (size == 0 || size > maxReadMemoryBytes) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument,
                      std::format("read size {} (1..{} bytes)", size, maxReadMemoryBytes)));
    }
    std::vector<std::byte> buffer(size);
    const int count = m_api.readMemory(address, buffer);
    if (count < 0 || std::cmp_less(count, size)) {
        return std::unexpected(
            makeError(ErrorCode::TargetConnectFailed,
                      libraryDetail(std::format("JLINKARM_ReadMemEx(0x{:08X}, {}) returned {}",
                                                address, size, count))));
    }
    return buffer;
}

Status JLinkProgrammer::reset(ResetMode mode) { return m_probe.reset(mode == ResetMode::Halt); }

} // namespace hio::jlink

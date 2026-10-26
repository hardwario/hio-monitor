#include "progress_ui.h"

#include "cli_app.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/device/operation.h"
#include "hio/firmware/program_progress.h"
#include "output.h"

#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

namespace hio::cli {

namespace {

constexpr std::size_t barWidth = 30;

} // namespace

ProgressUi::ProgressUi(const Context& context, std::ostream& err, bool tty, bool quiet)
    : m_context(&context), m_err(&err), m_tty(tty), m_quiet(quiet) {}

ProgressUi::~ProgressUi() { finish(); }

std::string_view ProgressUi::label(std::string_view phaseId) {
    if (phaseId == "connecting") {
        return "Connecting";
    }
    if (phaseId == "downloading") {
        return "Downloading";
    }
    if (phaseId == "switchingTarget") {
        return "Switching target";
    }
    if (phaseId == "erasing") {
        return "Erasing";
    }
    if (phaseId == "programming") {
        return "Programming";
    }
    if (phaseId == "verifying") {
        return "Verifying";
    }
    if (phaseId == "modem") {
        return "Modem firmware";
    }
    if (phaseId == "recovering") {
        return "Recovering";
    }
    if (phaseId == "resetting") {
        return "Resetting";
    }
    if (phaseId == "restoring") {
        return "Restoring";
    }
    return phaseId;
}

std::string ProgressUi::barLine(std::string_view label, std::uint64_t done, std::uint64_t total,
                                int step, int steps) {
    std::string line = steps > 1 ? std::format("[{}/{}] ", step, steps) : std::string();
    line += std::format("{:<16}", label);
    if (total == 0) {
        return line + "...";
    }
    const std::uint64_t clamped = std::min(done, total);
    const auto filled = static_cast<std::size_t>(clamped * barWidth / total);
    line += "[" + std::string(filled, '#') + std::string(barWidth - filled, ' ') + "]";
    line += std::format(" {:>3} %", clamped * 100 / total);
    return line;
}

void ProgressUi::progress(const device::OperationProgress& progress) {
    this->progress(firmware::phaseId(progress.phase), progress.done, progress.total, progress.step,
                   progress.steps);
}

void ProgressUi::progress(std::string_view phaseId, std::uint64_t done, std::uint64_t total,
                          int step, int steps) {
    if (m_quiet) {
        return;
    }
    if (m_context->mode == OutputMode::Json) {
        QJsonObject event;
        event.insert(QStringLiteral("type"), QStringLiteral("progress"));
        event.insert(QStringLiteral("step"), step);
        event.insert(QStringLiteral("steps"), steps);
        event.insert(QStringLiteral("phase"), toQString(phaseId));
        event.insert(QStringLiteral("done"), static_cast<double>(done));
        event.insert(QStringLiteral("total"), static_cast<double>(total));
        const std::string text = toCompactUtf8(event);
        if (text != m_lastJson && m_context->output != nullptr) {
            m_lastJson = text;
            m_context->output->printEvent(event, {});
        }
        return;
    }
    const bool newPhase = phaseId != m_phase || step != m_step;
    if (!m_tty) {
        if (newPhase) {
            *m_err << (steps > 1 ? std::format("[{}/{}] ", step, steps) : std::string())
                   << label(phaseId) << "...\n"
                   << std::flush;
        }
        m_phase = std::string(phaseId);
        m_step = step;
        return;
    }
    const int percent = total == 0 ? -1 : static_cast<int>(std::min(done, total) * 100 / total);
    if (!newPhase && percent == m_lastPercent) {
        return;
    }
    if (newPhase && m_lineOpen) {
        *m_err << '\n';
    }
    *m_err << '\r' << barLine(label(phaseId), done, total, step, steps) << std::flush;
    m_lineOpen = true;
    m_lastPercent = percent;
    m_phase = std::string(phaseId);
    m_step = step;
}

void ProgressUi::warning(const Error& error) {
    if (m_context->mode == OutputMode::Json) {
        QJsonObject event;
        event.insert(QStringLiteral("type"), QStringLiteral("warning"));
        event.insert(QStringLiteral("error"), toJson(error));
        if (m_context->output != nullptr) {
            m_context->output->printEvent(event, {});
        }
        return;
    }
    finish();
    *m_err << "Warning: " << error.message;
    if (!error.detail.empty()) {
        *m_err << " (" << error.detail << ")";
    }
    *m_err << '\n' << std::flush;
}

void ProgressUi::info(std::string_view text) {
    if (m_context->mode == OutputMode::Json) {
        return;
    }
    finish();
    *m_err << text << '\n' << std::flush;
}

void ProgressUi::finish() {
    if (m_lineOpen) {
        *m_err << '\n' << std::flush;
        m_lineOpen = false;
    }
}

} // namespace hio::cli

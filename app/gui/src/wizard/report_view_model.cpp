#include "wizard/report_view_model.h"

#include "hio/core/result.h"
#include "hio/diag/report.h"
#include "workers/diag_worker.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>

namespace hio::gui {

ReportViewModel::ReportViewModel(DiagWorker& worker, Hooks hooks, QObject* parent)
    : QObject(parent), m_worker(worker), m_hooks(std::move(hooks)) {
    connect(&worker, &DiagWorker::reportCollected, this,
            [this](quint64 generation, const std::shared_ptr<const diag::ReportInput>& input,
                   const QString& summary) {
                if (generation != m_generation || !m_open) {
                    return; // stale
                }
                m_generation = 0;
                m_input = input;
                m_summary = summary;
                setState(QStringLiteral("ready"));
            });
    connect(&worker, &DiagWorker::reportSaved, this,
            [this](quint64 generation, const Result<std::size_t>& bytes) {
                if (generation != m_generation) {
                    return;
                }
                m_generation = 0;
                if (!bytes) {
                    m_error = ErrorInfo(bytes.error());
                    setState(QStringLiteral("error"));
                    return;
                }
                if (m_hooks.toast) {
                    m_hooks.toast(tr("Diagnostic report saved."));
                }
                setState(QStringLiteral("saved"));
            });
}

QUrl ReportViewModel::defaultFolder() {
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

void ReportViewModel::show() {
    if (m_open) {
        return;
    }
    m_open = true;
    m_summary.clear();
    m_error = ErrorInfo();
    m_savedPath.clear();
    m_input.reset();
    m_fileName = QString::fromStdString(diag::defaultReportFileName());
    DiagWorker::CollectRequest request;
    request.context = m_hooks.environment ? m_hooks.environment() : diag::EnvironmentContext{};
    request.tabs = m_hooks.tabs ? m_hooks.tabs() : QJsonArray();
    request.language = m_hooks.language ? m_hooks.language() : QStringLiteral("en");
    m_generation = m_worker.collectReport(std::move(request));
    setState(QStringLiteral("collecting"));
}

void ReportViewModel::close() {
    if (m_generation != 0) {
        m_worker.cancel(m_generation);
        m_generation = 0;
    }
    m_open = false;
    m_input.reset();
    emit changed();
}

void ReportViewModel::save(const QUrl& file) {
    if (!m_input || m_generation != 0 || !file.isLocalFile()) {
        return;
    }
    const QString path = file.toLocalFile();
    m_savedPath = QDir::toNativeSeparators(path);
    m_error = ErrorInfo();
    m_generation = m_worker.saveReport(DiagWorker::SaveRequest{
        .input = m_input, .target = std::filesystem::path(path.toStdU16String())});
    setState(QStringLiteral("saving"));
}

void ReportViewModel::copySummary() {
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(m_summary);
        if (m_hooks.toast) {
            m_hooks.toast(tr("Summary copied."));
        }
    }
}

void ReportViewModel::showInFolder() {
    if (m_savedPath.isEmpty()) {
        return;
    }
    const QUrl folder = QUrl::fromLocalFile(QFileInfo(m_savedPath).absolutePath());
    if (m_hooks.openFolder) {
        m_hooks.openFolder(folder);
    } else {
        QDesktopServices::openUrl(folder);
    }
}

void ReportViewModel::setState(const QString& state) {
    m_state = state;
    emit changed();
}

} // namespace hio::gui

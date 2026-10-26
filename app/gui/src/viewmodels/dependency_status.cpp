#include "viewmodels/dependency_status.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "settings_store.h"
#include "viewmodels/error_info.h"
#include "workers/firmware_loader.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

namespace hio::gui {

namespace {

const QString jlinkPathKey = QStringLiteral("jlink/libraryPath");
const QString nrfUtilPathKey = QStringLiteral("nrfutil/path");

QString pathText(const std::filesystem::path& path) { return toQString(toUtf8(path)); }

} // namespace

DependencyStatus::DependencyStatus(FirmwareLoader& loader, SettingsStore* settings, Hooks hooks,
                                   QObject* parent)
    : QObject(parent), m_loader(loader), m_hooks(std::move(hooks)) {
    connect(&m_loader, &FirmwareLoader::jlinkChecked, this, &DependencyStatus::onJLinkChecked);
    connect(&m_loader, &FirmwareLoader::nrfUtilChecked, this, &DependencyStatus::onNrfUtilChecked);
    if (settings != nullptr) {
        // A changed path (Settings page, setup wizard 0008) invalidates the cached result.
        connect(settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
            if (key == jlinkPathKey && m_jlinkState != Unknown) {
                checkJLink();
            } else if (key == nrfUtilPathKey && m_nrfUtilState != Unknown) {
                checkNrfUtil();
            }
        });
    }
}

QString DependencyStatus::jlinkPath() const {
    return m_jlink ? pathText(m_jlink->path) : QString();
}

QString DependencyStatus::jlinkVersion() const {
    return m_jlink && m_jlink->version != 0
               ? toQString(jlink::formatLibraryVersion(m_jlink->version))
               : QString();
}

QString DependencyStatus::jlinkSourceText() const {
    if (!m_jlink) {
        return {};
    }
    switch (m_jlink->source) {
    case jlink::LibrarySource::Option:
        return tr("Set on the command line");
    case jlink::LibrarySource::Environment:
        return tr("Set by the environment variable %1")
            .arg(toQString(jlink::libraryEnvironmentVariable));
    case jlink::LibrarySource::Setting:
        return tr("Custom path");
    case jlink::LibrarySource::Discovered:
        return tr("Detected automatically");
    }
    return {};
}

ErrorInfo DependencyStatus::jlinkError() const {
    return m_jlinkProblem ? ErrorInfo(*m_jlinkProblem) : ErrorInfo();
}

QString DependencyStatus::nrfUtilPath() const {
    return m_nrfUtilExecutable ? pathText(*m_nrfUtilExecutable) : QString();
}

QString DependencyStatus::nrfUtilVersion() const {
    return m_nrfUtil ? formatNrfUtilVersion(*m_nrfUtil) : QString();
}

ErrorInfo DependencyStatus::nrfUtilError() const {
    return m_nrfUtilProblem ? ErrorInfo(*m_nrfUtilProblem) : ErrorInfo();
}

QString DependencyStatus::formatNrfUtilVersion(const nrfutil::NrfUtilInfo& info) {
    return tr("%1 (device command %2)")
        .arg(toQString(nrfutil::formatVersion(info.core)),
             toQString(nrfutil::formatVersion(info.device)));
}

void DependencyStatus::ensureChecked() {
    if (m_jlinkState == Unknown) {
        checkJLink();
    }
    if (m_nrfUtilState == Unknown) {
        checkNrfUtil();
    }
}

void DependencyStatus::refresh() {
    checkJLink();
    checkNrfUtil();
}

void DependencyStatus::checkJLink() {
    if (!m_hooks.jlinkOptions) {
        return;
    }
    m_jlinkGeneration = m_loader.checkJLink(&m_statusTag, m_hooks.jlinkOptions());
    m_jlinkState = Checking;
    jlinkChanged(); // signal
}

void DependencyStatus::checkNrfUtil() {
    if (!m_hooks.nrfUtilOptions) {
        return;
    }
    m_nrfUtilGeneration = m_loader.checkNrfUtil(&m_statusTag, m_hooks.nrfUtilOptions());
    m_nrfUtilState = Checking;
    nrfUtilChanged(); // signal
}

std::uint64_t DependencyStatus::checkJLinkCandidate(const std::filesystem::path& path) {
    jlink::LocatorOptions options =
        m_hooks.jlinkOptions ? m_hooks.jlinkOptions() : jlink::LocatorOptions{};
    options.option = path; // the highest precedence: exactly this file
    m_jlinkCandidateGeneration = m_loader.checkJLink(&m_candidateTag, std::move(options));
    return m_jlinkCandidateGeneration;
}

std::uint64_t DependencyStatus::checkNrfUtilCandidate(const std::filesystem::path& path) {
    nrfutil::LocatorOptions options =
        m_hooks.nrfUtilOptions ? m_hooks.nrfUtilOptions() : nrfutil::LocatorOptions{};
    options.option = path;
    m_nrfUtilCandidateGeneration = m_loader.checkNrfUtil(&m_candidateTag, std::move(options));
    return m_nrfUtilCandidateGeneration;
}

void DependencyStatus::retranslate() {
    jlinkChanged();   // signal
    nrfUtilChanged(); // signal
}

void DependencyStatus::onJLinkChecked(quint64 generation, const Result<JLinkFound>& result) {
    if (generation == m_jlinkCandidateGeneration) {
        jlinkCandidateChecked(generation, result); // signal
        return;
    }
    if (generation != m_jlinkGeneration) {
        return; // another requester's or a stale result
    }
    if (result) {
        m_jlink = *result;
        m_jlinkProblem.reset();
        m_jlinkState = Found;
    } else {
        m_jlink.reset();
        m_jlinkProblem = result.error();
        m_jlinkState = Problem;
        qCInfo(lcGui).noquote() << "J-Link library:" << toQString(result.error().detail);
    }
    jlinkChanged(); // signal
}

void DependencyStatus::onNrfUtilChecked(quint64 generation, const NrfUtilStatus& status) {
    if (generation == m_nrfUtilCandidateGeneration) {
        nrfUtilCandidateChecked(generation, status); // signal
        return;
    }
    if (generation != m_nrfUtilGeneration) {
        return;
    }
    m_nrfUtilExecutable = status.executable;
    if (status.info) {
        m_nrfUtil = *status.info;
        m_nrfUtilProblem.reset();
        m_nrfUtilState = Found;
    } else {
        m_nrfUtil.reset();
        m_nrfUtilProblem = status.info.error();
        m_nrfUtilState = Problem;
        qCInfo(lcGui).noquote() << "nRF Util:" << toQString(status.info.error().detail);
    }
    nrfUtilChanged(); // signal
}

} // namespace hio::gui

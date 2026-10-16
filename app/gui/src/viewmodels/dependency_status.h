#pragma once

// DependencyStatus - J-Link library and nRF Util detection for the Settings page (J-Link, nRF Util
// and About sections) and the Flash page's prerequisites (plan/features/0006-m3-flash-settings.md,
// E1; ADR-0006, ADR-0016, ADR-0029, ADR-0042).
//
// The detection runs on the FirmwareLoader thread (it loads a J-Link library copy to read its
// version and runs the nRF Util version probes, up to 10 s each). Results are cached until a path
// setting changes (jlink/libraryPath, nrfutil/path; also when the setup wizard saves one, 0008) or
// refresh() is called. Nothing is checked before the first ensureChecked() / refresh().
//
// Candidate paths (Settings page "Custom path") are checked the same way before they are saved
// (checkJLinkCandidate / checkNrfUtilCandidate): an invalid path is reported and never stored.
//
// Ownership: a std::unique_ptr member of FirmwareServices (no Qt parent), declared after the loader
// it uses and destroyed before it; QML reads it through a property (C++ ownership). GUI thread
// only.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "viewmodels/error_info.h"
#include "workers/firmware_loader.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace hio::gui {

class SettingsStore;

class DependencyStatus : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FirmwareServices")

    Q_PROPERTY(int jlinkState READ jlinkState NOTIFY jlinkChanged)
    Q_PROPERTY(QString jlinkPath READ jlinkPath NOTIFY jlinkChanged)
    Q_PROPERTY(QString jlinkVersion READ jlinkVersion NOTIFY jlinkChanged)
    // "Detected automatically", "Custom path", "Set by HIO_JLINK_LIBRARY" (translated).
    Q_PROPERTY(QString jlinkSourceText READ jlinkSourceText NOTIFY jlinkChanged)
    Q_PROPERTY(hio::gui::ErrorInfo jlinkError READ jlinkError NOTIFY jlinkChanged)
    Q_PROPERTY(int nrfUtilState READ nrfUtilState NOTIFY nrfUtilChanged)
    Q_PROPERTY(QString nrfUtilPath READ nrfUtilPath NOTIFY nrfUtilChanged)
    Q_PROPERTY(QString nrfUtilVersion READ nrfUtilVersion NOTIFY nrfUtilChanged)
    Q_PROPERTY(hio::gui::ErrorInfo nrfUtilError READ nrfUtilError NOTIFY nrfUtilChanged)

public:
    // Unscoped: QML compares DependencyStatus.Found etc. with the int properties.
    enum State : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        Unknown,          // not checked yet
        Checking,
        Found,
        Problem, // not found, not loadable, too old, ... (the error properties)
    };
    Q_ENUM(State)

    struct Hooks {
        std::function<jlink::LocatorOptions()> jlinkOptions;     // environment, setting, search
        std::function<nrfutil::LocatorOptions()> nrfUtilOptions; // environment, setting, PATH
    };

    DependencyStatus(FirmwareLoader& loader, SettingsStore* settings, Hooks hooks,
                     QObject* parent = nullptr);

    [[nodiscard]] int jlinkState() const noexcept { return m_jlinkState; }
    [[nodiscard]] QString jlinkPath() const;
    [[nodiscard]] QString jlinkVersion() const;
    [[nodiscard]] QString jlinkSourceText() const;
    [[nodiscard]] ErrorInfo jlinkError() const;
    [[nodiscard]] int nrfUtilState() const noexcept { return m_nrfUtilState; }
    [[nodiscard]] QString nrfUtilPath() const;
    [[nodiscard]] QString nrfUtilVersion() const;
    [[nodiscard]] ErrorInfo nrfUtilError() const;
    // The nRF Util error (nullopt while unknown, checking or found).
    [[nodiscard]] const std::optional<Error>& nrfUtilProblem() const noexcept {
        return m_nrfUtilProblem;
    }

    // Checks both tools unless they were checked already (or are being checked).
    void ensureChecked();
    // Checks both tools again.
    Q_INVOKABLE void refresh();

    // Checks a path before it is saved; the result arrives as jlinkCandidateChecked /
    // nrfUtilCandidateChecked with the returned generation.
    std::uint64_t checkJLinkCandidate(const std::filesystem::path& path);
    std::uint64_t checkNrfUtilCandidate(const std::filesystem::path& path);

    // A live language switch (texts that are built here).
    void retranslate();

    // "7.13.0 (device 2.8.1)".
    [[nodiscard]] static QString formatNrfUtilVersion(const nrfutil::NrfUtilInfo& info);

signals:
    void jlinkChanged();
    void nrfUtilChanged();
    void jlinkCandidateChecked(quint64 generation, const hio::Result<hio::gui::JLinkFound>& result);
    void nrfUtilCandidateChecked(quint64 generation, const hio::gui::NrfUtilStatus& status);

private:
    void checkJLink();
    void checkNrfUtil();
    void onJLinkChecked(quint64 generation, const Result<JLinkFound>& result);
    void onNrfUtilChecked(quint64 generation, const NrfUtilStatus& status);

    FirmwareLoader& m_loader;
    Hooks m_hooks;
    State m_jlinkState = Unknown;
    State m_nrfUtilState = Unknown;
    std::uint64_t m_jlinkGeneration = 0;
    std::uint64_t m_nrfUtilGeneration = 0;
    std::uint64_t m_jlinkCandidateGeneration = 0;
    std::uint64_t m_nrfUtilCandidateGeneration = 0;
    std::optional<JLinkFound> m_jlink;
    std::optional<Error> m_jlinkProblem;
    std::optional<std::filesystem::path> m_nrfUtilExecutable;
    std::optional<nrfutil::NrfUtilInfo> m_nrfUtil;
    std::optional<Error> m_nrfUtilProblem;
    // Requester tags of the loader queue (status checks and candidate checks never replace each
    // other).
    const int m_statusTag = 0;
    const int m_candidateTag = 0;
};

} // namespace hio::gui

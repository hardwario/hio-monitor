#pragma once

// ReportViewModel - the "Create diagnostic report" dialog (plan/features/0007 § Diagnostic report
// dialog, ADR-0075): collects the doctor checks and the report data on the DiagWorker (≤ 15 s),
// shows summary.txt, saves the zip where the user chooses (native file dialog in QML) and copies
// the summary. Nothing is sent anywhere. Exposed as AppViewModel.report. GUI thread only.

#include "viewmodels/error_info.h"
#include "workers/diag_worker.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration>

#include <cstdint>
#include <functional>
#include <memory>

namespace hio::gui {

class ReportViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    // "collecting" | "ready" | "saving" | "saved" | "error"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(hio::gui::ErrorInfo error READ error NOTIFY changed)
    Q_PROPERTY(QString defaultFileName READ defaultFileName NOTIFY changed)
    Q_PROPERTY(QUrl defaultFolder READ defaultFolder CONSTANT) // Documents
    Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)

public:
    struct Hooks {
        std::function<diag::EnvironmentContext()> environment; // GUI thread (settings)
        std::function<QJsonArray()> tabs;                      // the open tabs
        std::function<QString()> language;                     // "en", "cs"
        std::function<void(QString)> toast;
        std::function<void(QUrl)> openFolder; // empty: QDesktopServices::openUrl
    };

    ReportViewModel(DiagWorker& worker, Hooks hooks, QObject* parent = nullptr);

    [[nodiscard]] bool isOpen() const noexcept { return m_open; }
    [[nodiscard]] QString state() const { return m_state; }
    [[nodiscard]] QString summary() const { return m_summary; }
    [[nodiscard]] ErrorInfo error() const { return m_error; }
    [[nodiscard]] QString defaultFileName() const { return m_fileName; }
    [[nodiscard]] static QUrl defaultFolder();
    [[nodiscard]] QString savedPath() const { return m_savedPath; }

    // Opens the dialog and starts collecting (Help → Create diagnostic report…).
    Q_INVOKABLE void show();
    // Cancel / Close: stops collecting or saving and closes.
    Q_INVOKABLE void close();
    // Saves the zip to `file` (a local file URL from the file dialog).
    Q_INVOKABLE void save(const QUrl& file);
    Q_INVOKABLE void copySummary();
    Q_INVOKABLE void showInFolder();

signals:
    void changed();

private:
    void setState(const QString& state);

    DiagWorker& m_worker;
    Hooks m_hooks;
    bool m_open = false;
    QString m_state = QStringLiteral("collecting");
    QString m_summary;
    ErrorInfo m_error;
    QString m_fileName;
    QString m_savedPath;
    std::uint64_t m_generation = 0; // the request whose result is awaited (0 = none)
    std::shared_ptr<const diag::ReportInput> m_input;
};

} // namespace hio::gui

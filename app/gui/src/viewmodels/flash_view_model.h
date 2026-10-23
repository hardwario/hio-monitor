#pragma once

// FlashViewModel - the Flash page of a device tab (plan/features/0006-m3-flash-settings.md, D2-D4;
// ADR-0049, ADR-0054, ADR-0055, ADR-0057, ADR-0062, ADR-0038, ADR-0109).
//
// Kind first (ADR-0055; amended by 0021, ADR-0174): application firmware (target app) or LTE
// firmware (lte: the designated LTE firmware, or an own .hex, LTE bundle or modem zip - the
// operation follows the ADR-0040 classification: flash, flashLteBundle or updateModem). The source
// part (file, catalog ID, recent list, kind hint, UICR) is the child FirmwareSourceViewModel
// (amended by 0010). Flash and the maintenance actions (reset, erase code, erase all, recover) run
// as one session operation each on the tab's session with the kind's target (ADR-0057, ADR-0062):
//   - a tab without a working session opens one first (DeviceTabViewModel::ensureSession(): the
//     console target, amended by the 0004 review S2);
//   - progress (phases from OperationProgress::plannedPhases, step 1/2 of an LTE bundle) and the
//   one
//     result arrive through the session's queued signals; events of other operations are ignored;
//   - the page is busy while the operation runs (setBusy: rail badge, "Flashing…" in the device
//     bar, close/quit confirmation that cancels); the shell adds the console markers.
// The session is held only as QPointer and re-read on sessionChanged (review S3). A session that
// goes away during an operation ends it as failed with the session's error.
//
// Before starting, the page warns when the kind's MCU is not the tab's console target: on
// CHESTER-M the other MCU is not reachable through the same connector (ADR-0057, Phase 0 result).
//
// Ownership: child of its DeviceTabViewModel (Qt parent, created by the page registry). GUI thread
// only.

#include "hio/device/operation.h"
#include "viewmodels/error_info.h"
#include "viewmodels/firmware_source_view_model.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace hio::device {
class DeviceSession;
struct DeviceProfile;
struct TargetProfile;
} // namespace hio::device

namespace hio::gui {

class DeviceTabViewModel;
class FirmwareServices;

class FlashViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the page registry")

    Q_PROPERTY(hio::gui::FirmwareSourceViewModel* source READ source CONSTANT)
    Q_PROPERTY(int kind READ kind WRITE setKind NOTIFY kindChanged)
    Q_PROPERTY(QString targetId READ targetId NOTIFY kindChanged)
    // [{kind, title, mcu, accepts}] for the kind cards.
    Q_PROPERTY(QVariantList kinds READ kinds NOTIFY textsChanged)
    // nRF Util missing / too old for modem firmware or an LTE bundle (ADR-0016).
    Q_PROPERTY(QString prerequisiteText READ prerequisiteText NOTIFY stateChanged)
    // ADR-0057 Phase 0: the kind's MCU is not the tab's console target.
    Q_PROPERTY(QString connectorWarning READ connectorWarning NOTIFY stateChanged)
    Q_PROPERTY(bool haltAvailable READ haltAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool halt READ halt WRITE setHalt NOTIFY haltChanged)
    Q_PROPERTY(bool canFlash READ canFlash NOTIFY stateChanged)
    Q_PROPERTY(QString cannotFlashReason READ cannotFlashReason NOTIFY stateChanged)
    Q_PROPERTY(QString flashSummary READ flashSummary NOTIFY stateChanged)
    Q_PROPERTY(int opState READ opState NOTIFY stateChanged)
    Q_PROPERTY(bool flashOperation READ flashOperation NOTIFY stateChanged)
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY stateChanged)
    // [{title, state: "done" | "current" | "pending"}] for the PhaseStepper.
    Q_PROPERTY(QVariantList phases READ phases NOTIFY progressChanged)
    Q_PROPERTY(int currentPhase READ currentPhase NOTIFY progressChanged)
    Q_PROPERTY(int step READ step NOTIFY progressChanged)
    Q_PROPERTY(int steps READ steps NOTIFY progressChanged)
    Q_PROPERTY(QString stepText READ stepText NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged) // 0..1, -1 = indeterminate
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(bool cancelAvailable READ cancelAvailable NOTIFY stateChanged)
    // "The console is paused and resumes automatically after flashing." while it applies.
    Q_PROPERTY(QString consoleNote READ consoleNote NOTIFY stateChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY stateChanged)
    Q_PROPERTY(QString chipText READ chipText NOTIFY stateChanged)
    Q_PROPERTY(hio::gui::ErrorInfo error READ error NOTIFY stateChanged)
    // The result ErrorPanel's primary action: Firmware errors keep "Choose another file"; other
    // errors of a flash retry it (Retry), of a maintenance action go back to the form (Dismiss).
    Q_PROPERTY(int errorAction READ errorAction NOTIFY stateChanged)
    Q_PROPERTY(QString maintenanceTitle READ maintenanceTitle NOTIFY kindChanged)
    // [Erase application] (code only) exists only where the profile's default erase is code only
    // (CHESTER app); an LTE MCU is always erased completely behind the confirmation (ADR-0038).
    Q_PROPERTY(bool eraseCodeAvailable READ eraseCodeAvailable NOTIFY kindChanged)
    Q_PROPERTY(bool canRunMaintenance READ canRunMaintenance NOTIFY stateChanged)
    Q_PROPERTY(QString nrfUtilInstallUrl READ nrfUtilInstallUrl CONSTANT)
    // The ConfirmDialog texts of the pending erase-all / recover (ADR-0038).
    Q_PROPERTY(QVariantMap confirmation READ confirmation NOTIFY confirmationRequested)

public:
    // Unscoped: QML compares FlashViewModel.Running etc. with the int property.
    enum OpState : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        Idle,
        Running,
        Cancelling,
        Succeeded,
        Failed,
        Cancelled,
    };
    Q_ENUM(OpState)

    static inline const QString pageId = QStringLiteral("flash");

    FlashViewModel(DeviceTabViewModel* tab, FirmwareServices& services);
    ~FlashViewModel() override;
    FlashViewModel(const FlashViewModel&) = delete;
    FlashViewModel& operator=(const FlashViewModel&) = delete;
    FlashViewModel(FlashViewModel&&) = delete;
    FlashViewModel& operator=(FlashViewModel&&) = delete;

    [[nodiscard]] FirmwareSourceViewModel* source() const { return m_source; }
    [[nodiscard]] int kind() const noexcept { return m_kind; }
    void setKind(int kind);
    [[nodiscard]] QString targetId() const;
    [[nodiscard]] QVariantList kinds() const;
    [[nodiscard]] QString prerequisiteText() const;
    [[nodiscard]] QString connectorWarning() const;
    [[nodiscard]] bool haltAvailable() const;
    [[nodiscard]] bool halt() const noexcept { return m_halt; }
    void setHalt(bool halt);
    [[nodiscard]] bool canFlash() const { return cannotFlashReason().isEmpty(); }
    [[nodiscard]] QString cannotFlashReason() const;
    [[nodiscard]] QString flashSummary() const;
    [[nodiscard]] int opState() const noexcept { return m_opState; }
    [[nodiscard]] bool flashOperation() const;
    [[nodiscard]] QString operationTitle() const;
    [[nodiscard]] QVariantList phases() const;
    [[nodiscard]] int currentPhase() const noexcept { return m_currentPhase; }
    [[nodiscard]] int step() const noexcept { return m_step; }
    [[nodiscard]] int steps() const noexcept { return m_steps; }
    [[nodiscard]] QString stepText() const;
    [[nodiscard]] qreal progress() const;
    [[nodiscard]] QString progressText() const;
    [[nodiscard]] bool cancelAvailable() const;
    [[nodiscard]] QString consoleNote() const;
    [[nodiscard]] QString resultText() const;
    [[nodiscard]] QString chipText() const;
    [[nodiscard]] ErrorInfo error() const;
    [[nodiscard]] int errorAction() const;
    [[nodiscard]] QString maintenanceTitle() const;
    [[nodiscard]] bool eraseCodeAvailable() const;
    [[nodiscard]] bool canRunMaintenance() const;
    [[nodiscard]] static QString nrfUtilInstallUrl();

    Q_INVOKABLE void flash();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset(bool halt);
    // Refused where eraseCodeAvailable is false.
    Q_INVOKABLE void eraseCode();
    // ConfirmDialog texts (ADR-0038): {title, body, items, consequence, acceptText}.
    Q_INVOKABLE [[nodiscard]] QVariantMap describeEraseAll() const;
    Q_INVOKABLE void eraseAll();
    Q_INVOKABLE [[nodiscard]] QVariantMap describeRecover() const;
    Q_INVOKABLE void recover();
    // [Erase everything…] / [Recover…]: prepares the confirmation and emits
    // confirmationRequested (QML opens the ConfirmDialog); confirm() runs the confirmed action on
    // the MCU the dialog named, and refuses it when the kind's MCU or the device changed since.
    Q_INVOKABLE void requestConfirmation(bool recover);
    Q_INVOKABLE void confirm();
    [[nodiscard]] QVariantMap confirmation() const { return m_confirmation; }
    // Back from the result card to the form (the source stays loaded).
    Q_INVOKABLE void dismissResult();
    // [Open console] of the success card.
    Q_INVOKABLE void openConsole();
    // The result ErrorPanel's primary action (ErrorAction): ChooseFile -> back to the form and
    // chooseFileRequested; OpenSettings -> back to the form and settingsRequested; Retry -> the
    // same flash request again (the image, MCU and options of the failed flash, whatever the
    // source card holds now); maintenance: back to the form.
    Q_INVOKABLE void triggerErrorAction(int action);

    // The phase titles of the stepper (translated), exposed for tests.
    [[nodiscard]] static QString phaseTitle(firmware::ProgramPhase phase);

signals:
    void kindChanged();
    void haltChanged();
    void stateChanged();
    void progressChanged();
    void textsChanged();
    void chooseFileRequested();
    // QML opens the Settings tab at `sectionId` (0021: "nrfutil", "jlink" or "").
    void settingsRequested(const QString& sectionId);
    void confirmationRequested();
    // A text for screen readers (operation started / finished; design system § 6).
    void announcement(const QString& text);

private:
    // One flash as started (kept for Retry).
    struct FlashRequest {
        device::OperationKind kind = device::OperationKind::Flash; // Flash, LteBundle or Modem
        QString targetId;
        LoadedSource source;
        device::FlashOptions options;
        QString sourceName;
    };
    // The maintenance action of the open ConfirmDialog and the MCU it names.
    struct PendingConfirmation {
        bool recover = false;
        QString targetId;
        QString profileId;
    };
    struct RunningOperation {
        device::OperationId id = 0;
        device::OperationKind kind = device::OperationKind::Flash;
        QString targetId;
        QString sourceName; // the file of a flash
        QPointer<device::DeviceSession> session;
    };

    void runFlash(const FlashRequest& request);
    void runEraseAll(const QString& target);
    void runRecover(const QString& target);
    void start(device::OperationKind kind, const QString& target,
               const std::function<Result<device::OperationId>(device::DeviceSession&)>& run);
    void onSessionChanged();
    void onProgress(device::OperationId id, const device::OperationProgress& progress);
    void onFinished(device::OperationId id, const device::OperationResult& result);
    void finish(device::OperationKind kind, const Status& status,
                std::chrono::milliseconds duration, const std::optional<device::ChipInfo>& chip);
    void applyDefaultKind();
    void retranslate();
    [[nodiscard]] const device::DeviceProfile* profile() const;
    [[nodiscard]] const device::TargetProfile* targetProfile(const QString& id) const;
    [[nodiscard]] bool otherOperationRunning() const;
    [[nodiscard]] bool needsNrfUtil() const;
    [[nodiscard]] static QString busyText(device::OperationKind kind);
    [[nodiscard]] QVariantMap describeErase(bool recover) const;

    DeviceTabViewModel* m_tab; // parent
    FirmwareServices& m_services;
    FirmwareSourceViewModel* m_source = nullptr; // child
    FirmwareSourceViewModel::Kind m_kind = FirmwareSourceViewModel::AppFirmware;
    // The kind was chosen in this tab (ADR-0055: remembered per tab for the session, in memory
    // only); until then it follows the tab's target.
    bool m_kindChosen = false;
    bool m_halt = false;
    QPointer<device::DeviceSession> m_session;
    std::vector<QMetaObject::Connection> m_sessionConnections;
    std::optional<RunningOperation> m_running;
    OpState m_opState = Idle;
    device::OperationKind m_lastKind = device::OperationKind::Flash;
    QString m_lastTarget;
    QString m_lastSourceName;
    std::optional<FlashRequest> m_lastFlash; // Retry
    std::span<const firmware::ProgramPhase> m_plannedPhases;
    int m_currentPhase = -1;
    int m_step = 1;
    int m_steps = 1;
    std::uint64_t m_done = 0;
    std::uint64_t m_total = 0;
    firmware::ProgramPhase m_phase = firmware::ProgramPhase::Connecting;
    std::chrono::milliseconds m_elapsed{};
    std::chrono::milliseconds m_duration{};
    std::optional<device::ChipInfo> m_chip;
    std::optional<Error> m_error;
    bool m_cancelRequested = false;
    bool m_resetHalt = false; // of the last reset (result text)
    QVariantMap m_confirmation;
    std::optional<PendingConfirmation> m_pending; // of the open confirmation
};

} // namespace hio::gui

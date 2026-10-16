#pragma once

// FirmwareSourceViewModel - the source part of the Flash page
// (plan/features/0006-m3-flash-settings.md, D1; amended by 0010: also used by the flash wizard):
// kind-filtered loading of a file, the catalog download by ID (ADR-0037, ADR-0054), the recent
// list, the summary, the kind hint (ADR-0040: the kind never changes silently), hasUicr and
// allowUicr (ADR-0036).
//
// The owner sets the kind (Flash page: FlashViewModel). Files are parsed by the application's
// FirmwareLoader worker; a newer request makes older results stale (generation). A loaded file is
// kept when the kind changes and re-evaluated for the new kind: when it does not fit, the source is
// Invalid with a hint ("This is LTE modem firmware — switch to LTE modem firmware?") and
// acceptSuggestedKind() asks the owner to switch (kindSuggestionAccepted). The recent list gets an
// entry only when a file is loaded or downloaded for a kind it fits, never by a kind change.
//
// Catalog downloads run on the GUI thread (FirmwareCatalog, QtNetwork); the M2 catalog validates
// the downloaded HEX itself (see the Implementation notes of 0006).
//
// Ownership: child of its owner (FlashViewModel, the flash wizard). The catalog operation it holds
// never outlives FirmwareServices (see firmware_services.h). GUI thread only.

#include "viewmodels/error_info.h"
#include "viewmodels/recent_sources_model.h"
#include "workers/firmware_loader.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace hio::device {
struct DeviceProfile;
struct TargetProfile;
} // namespace hio::device

namespace hio::firmware {
class CatalogOperation;
} // namespace hio::firmware

namespace hio::gui {

class FirmwareServices;

class FirmwareSourceViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FlashViewModel or the flash wizard")

    Q_PROPERTY(int kind READ kind NOTIFY kindChanged)
    Q_PROPERTY(int sourceState READ sourceState NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY sourceChanged)
    // 0..1 while downloading; -1 = unknown size.
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(bool hasUicr READ hasUicr NOTIFY sourceChanged)
    Q_PROPERTY(bool allowUicr READ allowUicr WRITE setAllowUicr NOTIFY allowUicrChanged)
    Q_PROPERTY(QString kindHint READ kindHint NOTIFY sourceChanged)
    Q_PROPERTY(int suggestedKind READ suggestedKind NOTIFY sourceChanged) // -1 = none
    Q_PROPERTY(hio::gui::ErrorInfo error READ error NOTIFY sourceChanged)
    // The inline validator text of the ID field ("" = valid or empty).
    Q_PROPERTY(QString catalogIdError READ catalogIdError NOTIFY catalogIdErrorChanged)
    Q_PROPERTY(bool catalogAvailable READ catalogAvailable NOTIFY kindChanged)
    Q_PROPERTY(QString catalogUrl READ catalogUrl NOTIFY kindChanged)
    // Modem firmware or an LTE bundle: "Modem update takes 1–3 minutes ...".
    Q_PROPERTY(bool modemNoteVisible READ modemNoteVisible NOTIFY sourceChanged)
    Q_PROPERTY(QStringList nameFilters READ nameFilters NOTIFY kindChanged)
    Q_PROPERTY(QString dropText READ dropText NOTIFY kindChanged)
    Q_PROPERTY(hio::gui::RecentSourcesModel* recent READ recent CONSTANT)

public:
    // Unscoped: QML compares FirmwareSourceViewModel.AppFirmware etc. with the int properties.
    enum Kind : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        AppFirmware,     // application MCU: .hex or catalog ID
        LteFirmware,     // LTE MCU: .hex, LTE bundle .zip or catalog ID
        ModemFirmware,   // modem core via nRF Util: mfw_nrf91*.zip
    };
    Q_ENUM(Kind)

    enum SourceState : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        None,
        Loading,
        Downloading,
        Ready,
        Invalid, // an error or a file of another kind (kindHint)
    };
    Q_ENUM(SourceState)

    // `profile` returns the device profile of the owner's tab (nullptr while none is chosen).
    FirmwareSourceViewModel(FirmwareServices& services,
                            std::function<const device::DeviceProfile*()> profile, QObject* parent);
    ~FirmwareSourceViewModel() override;
    FirmwareSourceViewModel(const FirmwareSourceViewModel&) = delete;
    FirmwareSourceViewModel& operator=(const FirmwareSourceViewModel&) = delete;
    FirmwareSourceViewModel(FirmwareSourceViewModel&&) = delete;
    FirmwareSourceViewModel& operator=(FirmwareSourceViewModel&&) = delete;

    // "app" for AppFirmware, "lte" for LteFirmware and ModemFirmware.
    [[nodiscard]] static QString targetIdFor(Kind kind);
    // "app", "lte", "modem" (flash/recent).
    [[nodiscard]] static QString kindId(Kind kind);

    [[nodiscard]] int kind() const noexcept { return m_kind; }
    // Set by the owner; filters loading, the recent list and the hint.
    void setKind(Kind kind);
    // The kind suggested for an Intel HEX file loaded as modem firmware (the owner's tab target).
    void setHexFallbackKind(Kind kind);

    [[nodiscard]] int sourceState() const noexcept { return m_state; }
    [[nodiscard]] QString sourceName() const;
    [[nodiscard]] QString sourceSummary() const;
    [[nodiscard]] qreal downloadProgress() const noexcept { return m_downloadProgress; }
    [[nodiscard]] bool hasUicr() const;
    [[nodiscard]] bool allowUicr() const noexcept { return m_allowUicr; }
    void setAllowUicr(bool allow);
    [[nodiscard]] QString kindHint() const;
    [[nodiscard]] int suggestedKind() const;
    [[nodiscard]] ErrorInfo error() const;
    [[nodiscard]] QString catalogIdError() const { return m_catalogIdError; }
    [[nodiscard]] bool catalogAvailable() const;
    [[nodiscard]] QString catalogUrl() const;
    [[nodiscard]] bool modemNoteVisible() const;
    [[nodiscard]] QStringList nameFilters() const;
    [[nodiscard]] QString dropText() const;
    [[nodiscard]] RecentSourcesModel* recent() const { return m_recent; }

    Q_INVOKABLE void loadFile(const QUrl& url);
    // Validates (catalogIdError) and starts the download; an invalid ID changes nothing else.
    Q_INVOKABLE void loadCatalogId(const QString& text);
    // Live validation of the ID field: false and catalogIdError for a malformed non-empty ID.
    Q_INVOKABLE bool checkCatalogId(const QString& text);
    Q_INVOKABLE void loadRecent(int row);
    Q_INVOKABLE void removeRecent(int row);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void cancelDownload();
    // [Switch] of the kind hint: emits kindSuggestionAccepted(suggestedKind).
    Q_INVOKABLE void acceptSuggestedKind();

    // The loaded source when it fits the kind (Ready), else nullptr.
    [[nodiscard]] const LoadedSource* readySource() const;
    // The loaded source whatever the kind (nullptr while none).
    [[nodiscard]] const LoadedSource* loadedSource() const;
    // A live language switch.
    void retranslate();

signals:
    void kindChanged();
    void sourceChanged();
    void downloadProgressChanged();
    void allowUicrChanged();
    void catalogIdErrorChanged();
    void kindSuggestionAccepted(int kind);

private:
    void loadPath(const QString& path);
    void onFileLoaded(quint64 generation, const Result<LoadedSource>& result);
    void onDownloadFinished();
    void setLoaded(LoadedSource loaded, QString recentSource);
    void setError(Error error);
    void setState(SourceState state);
    // After an aborted download: the loaded source again (or None).
    void restoreState();
    // The kind the loaded file belongs to, when it does not fit m_kind.
    [[nodiscard]] std::optional<Kind> mismatch() const;
    [[nodiscard]] const device::TargetProfile* target() const;

    FirmwareServices& m_services;
    std::function<const device::DeviceProfile*()> m_profile;
    RecentSourcesModel* m_recent = nullptr; // child
    Kind m_kind = AppFirmware;
    Kind m_hexFallback = AppFirmware;
    SourceState m_state = None;
    std::uint64_t m_generation = 0; // of the pending load
    std::unique_ptr<firmware::CatalogOperation> m_download;
    QString m_downloadId;
    qreal m_downloadProgress = -1;
    std::optional<LoadedSource> m_loaded;
    QString m_loadedName;   // file name or "Catalog ID <id>"
    QString m_pendingName;  // of the running load / download
    QString m_recentSource; // path or "catalog:<id>" of m_loaded
    std::optional<Error> m_error;
    bool m_allowUicr = false;
    QString m_catalogIdError;
};

} // namespace hio::gui

#include "viewmodels/firmware_source_view_model.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/profile.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "hio/firmware/firmware_image.h"
#include "pages/firmware_services.h"
#include "viewmodels/error_info.h"
#include "viewmodels/recent_sources_model.h"
#include "workers/firmware_loader.h"

#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QObject>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtTypes>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::gui {

namespace {

QString hexAddress(std::uint64_t address) {
    return QStringLiteral("0x") +
           QString::number(address, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

QString sizeText(std::uint64_t bytes) {
    return QLocale().formattedDataSize(static_cast<qint64>(bytes), 0, QLocale::DataSizeSIFormat);
}

// "2.1 MB" (one decimal, as in "Downloading… 2.1 / 6.1 MB").
QString megabytes(std::uint64_t bytes) {
    return QLocale().toString(static_cast<double>(bytes) / 1e6, 'f', 1);
}

// The version in a Nordic modem firmware file name ("mfw_nrf91x1_2.0.2.zip" -> "2.0.2"), "" when
// the name has none.
QString modemVersion(const QString& fileName) {
    static const QRegularExpression pattern(QStringLiteral("_(\\d+\\.\\d+\\.\\d+)\\.zip$"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(fileName);
    return match.hasMatch() ? match.captured(1) : QString();
}

} // namespace

FirmwareSourceViewModel::FirmwareSourceViewModel(
    FirmwareServices& services, std::function<const device::DeviceProfile*()> profile,
    QObject* parent)
    : QObject(parent), m_services(services), m_profile(std::move(profile)),
      // Qt parent ownership: this object.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      m_recent(new RecentSourcesModel(services.settings(), this)) {
    m_recent->setKind(kindId(m_kind));
    connect(&m_services.loader(), &FirmwareLoader::fileLoaded, this,
            &FirmwareSourceViewModel::onFileLoaded);
    connect(&m_services, &FirmwareServices::languageChanged, this,
            &FirmwareSourceViewModel::retranslate);
}

FirmwareSourceViewModel::~FirmwareSourceViewModel() {
    // The downloads first, while every member still exists: aborting them must not reach this
    // object.
    if (m_download) {
        m_download->disconnect(this);
        m_download.reset();
    }
    if (m_designatedDownload) {
        m_designatedDownload->disconnect(this);
        m_designatedDownload.reset();
    }
    if (m_lookup) {
        m_lookup->disconnect(this);
        m_lookup.reset();
    }
}

QString FirmwareSourceViewModel::targetIdFor(Kind kind) {
    return kind == AppFirmware ? QStringLiteral("app") : QStringLiteral("lte");
}

QString FirmwareSourceViewModel::kindId(Kind kind) {
    return kind == AppFirmware ? QStringLiteral("app") : QStringLiteral("lte");
}

void FirmwareSourceViewModel::setKind(Kind kind) {
    if (kind == m_kind) {
        return;
    }
    m_kind = kind;
    m_recent->setKind(kindId(kind));
    if (m_kind != AppFirmware) {
        // 0021 (ADR-0174): no catalog ID on the LTE card.
        cancelDownload();
        cancelLookup();
    }
    if (m_catalogIdError.size() > 0) {
        m_catalogIdError.clear();
        catalogIdErrorChanged(); // signal
    }
    kindChanged(); // signal
    if (m_loaded && (m_state == Ready || m_state == Invalid) && !m_error) {
        // Re-evaluated only: Recent gets an entry on an explicit load or download (review F7).
        setState(mismatch() ? Invalid : Ready);
    }
    sourceChanged(); // signal: hint, UICR of the other target, modem note
}

const device::TargetProfile* FirmwareSourceViewModel::target() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return profile != nullptr ? device::findTarget(*profile, toStdString(targetIdFor(m_kind)))
                              : nullptr;
}

const device::TargetProfile* FirmwareSourceViewModel::lteTarget() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return profile != nullptr ? device::findTarget(*profile, "lte") : nullptr;
}

int FirmwareSourceViewModel::sourceState() const {
    if (!designatedActive()) {
        return m_state;
    }
    switch (m_designatedState) {
    case DesignatedIdle:
        return None;
    case DesignatedDownloading:
        return Downloading;
    case DesignatedReady:
        return Ready;
    case DesignatedFailed:
        return Invalid;
    }
    return None;
}

QString FirmwareSourceViewModel::sourceName() const {
    if (designatedActive()) {
        const device::TargetProfile* lte = lteTarget();
        return lte != nullptr && lte->designatedFirmware
                   ? QStringLiteral("%1 %2").arg(toQString(lte->designatedFirmware->name),
                                                 toQString(lte->designatedFirmware->version))
                   : QString();
    }
    switch (m_state) {
    case Loading:
    case Downloading:
        return m_pendingName;
    case Ready:
    case Invalid:
        return m_loaded ? m_loadedName : m_pendingName;
    case None:
        break;
    }
    return {};
}

QString FirmwareSourceViewModel::sourceSummary() const {
    if (designatedActive()) {
        return m_designatedState == DesignatedReady
                   ? tr("%1 · LTE bundle (application + modem firmware)").arg(sourceName())
                   : QString();
    }
    if (m_state == Loading) {
        return tr("Loading %1…").arg(m_pendingName);
    }
    if (m_state == Downloading) {
        return tr("Downloading %1…").arg(m_pendingName);
    }
    if (!m_loaded || (m_state != Ready && m_state != Invalid)) {
        return {};
    }
    const LoadedSource& loaded = *m_loaded;
    switch (loaded.sourceClass) {
    case SourceClass::IntelHex: {
        QString range;
        if (loaded.image && !loaded.image->empty()) {
            const auto& segments = loaded.image->segments();
            range = QStringLiteral("%1–%2").arg(hexAddress(segments.front().address),
                                                hexAddress(segments.back().range().end));
        }
        if (m_kind == LteFirmware) {
            // 0021: summary per classification (ADR-0040).
            return tr("%1 · LTE application · %2 · %3")
                .arg(m_loadedName, sizeText(loaded.fileBytes), range);
        }
        return tr("%1 · Intel HEX · %2 · %3").arg(m_loadedName, sizeText(loaded.fileBytes), range);
    }
    case SourceClass::LteBundle:
        return tr("%1 · LTE bundle (application + modem firmware) · %2")
            .arg(m_loadedName, sizeText(loaded.fileBytes));
    case SourceClass::ModemZip: {
        const QString version = modemVersion(m_loadedName);
        return version.isEmpty() ? tr("%1 · Modem firmware (modem only) · %2")
                                       .arg(m_loadedName, sizeText(loaded.fileBytes))
                                 : tr("%1 · Modem firmware %2 (modem only) · %3")
                                       .arg(m_loadedName, version, sizeText(loaded.fileBytes));
    }
    }
    return {};
}

bool FirmwareSourceViewModel::hasUicr() const {
    const LoadedSource* source = readySource();
    const device::TargetProfile* targetProfile = target();
    return source != nullptr && source->image && targetProfile != nullptr &&
           source->image->intersects(targetProfile->uicr);
}

void FirmwareSourceViewModel::setAllowUicr(bool allow) {
    if (allow == m_allowUicr) {
        return;
    }
    m_allowUicr = allow;
    allowUicrChanged(); // signal
}

std::optional<FirmwareSourceViewModel::Kind> FirmwareSourceViewModel::mismatch() const {
    if (!m_loaded) {
        return std::nullopt;
    }
    // 0021 (ADR-0174): every LTE input (HEX, bundle, modem zip) fits the LTE kind; an LTE bundle
    // or modem zip on Application firmware is LTE firmware. A HEX file fits both kinds (ADR-0040
    // cannot tell the MCU of a HEX file).
    switch (m_loaded->sourceClass) {
    case SourceClass::IntelHex:
        return std::nullopt;
    case SourceClass::LteBundle:
    case SourceClass::ModemZip:
        return m_kind == LteFirmware ? std::nullopt : std::optional<Kind>(LteFirmware);
    }
    return std::nullopt;
}

QString FirmwareSourceViewModel::kindHint() const {
    if (designatedActive() || m_state != Invalid || m_error || !m_loaded) {
        return {};
    }
    const std::optional<Kind> other = mismatch();
    if (!other) {
        return {};
    }
    // Only towards LTE firmware: the ADR-0040 classification cannot tell the MCU of a HEX file, so
    // a HEX fits both kinds (user decision 2026-10-23: no "application firmware" hint).
    return tr("This is LTE firmware — switch to LTE firmware?");
}

int FirmwareSourceViewModel::suggestedKind() const {
    if (designatedActive() || m_state != Invalid || m_error) {
        return -1;
    }
    const std::optional<Kind> other = mismatch();
    return other ? static_cast<int>(*other) : -1;
}

ErrorInfo FirmwareSourceViewModel::error() const {
    return m_error && !designatedActive() ? ErrorInfo(*m_error) : ErrorInfo();
}

bool FirmwareSourceViewModel::catalogAvailable() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return m_kind == AppFirmware && (profile == nullptr || !profile->firmwareCatalogUrl.empty());
}

QUrl FirmwareSourceViewModel::docsUrl() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    if (profile == nullptr) {
        return {};
    }
    return {toQString(device::firmwareDocsUrlFor(*profile, toStdString(m_services.uiLanguage())))};
}

bool FirmwareSourceViewModel::modemNoteVisible() const {
    if (m_kind != LteFirmware) {
        return false;
    }
    if (designatedActive()) {
        return true; // the designated bundle always contains modem firmware
    }
    return m_loaded && (m_loaded->sourceClass == SourceClass::LteBundle ||
                        m_loaded->sourceClass == SourceClass::ModemZip);
}

QStringList FirmwareSourceViewModel::nameFilters() const {
    if (m_kind == AppFirmware) {
        return {tr("Intel HEX files (*.hex)"), tr("All files (*)")};
    }
    return {tr("LTE firmware (*.hex *.zip)"), tr("All files (*)")};
}

QString FirmwareSourceViewModel::dropText() const {
    if (m_kind == AppFirmware) {
        return tr("Drop a .hex file here");
    }
    return tr("Drop a .hex file, an LTE bundle .zip or a modem firmware .zip here");
}

// ---- 0021: the designated LTE firmware (ADR-0165) ----------------------------------------------

bool FirmwareSourceViewModel::designatedAvailable() const {
    const device::TargetProfile* lte = lteTarget();
    return lte != nullptr && lte->designatedFirmware.has_value();
}

bool FirmwareSourceViewModel::designatedActive() const {
    return m_kind == LteFirmware && m_lteSource == Designated && designatedAvailable();
}

void FirmwareSourceViewModel::setLteSource(int source) {
    if ((source != Designated && source != OwnFile) || source == m_lteSource) {
        return;
    }
    m_lteSource = static_cast<LteSource>(source);
    lteSourceChanged(); // signal
    sourceChanged();    // signal: the source of the LTE kind changed
}

QString FirmwareSourceViewModel::designatedTitle() const {
    const device::TargetProfile* lte = lteTarget();
    if (lte == nullptr || !lte->designatedFirmware) {
        return {};
    }
    return tr("%1 %2 (recommended)")
        .arg(toQString(lte->designatedFirmware->name), toQString(lte->designatedFirmware->version));
}

QString FirmwareSourceViewModel::designatedStatus() const {
    switch (m_designatedState) {
    case DesignatedDownloading:
        return m_designatedTotal > 0
                   ? tr("Downloading… %1 / %2 MB")
                         .arg(megabytes(m_designatedDone), megabytes(m_designatedTotal))
                   : tr("Downloading…");
    case DesignatedReady:
        return tr("Ready");
    case DesignatedIdle:
    case DesignatedFailed:
        break;
    }
    return {};
}

qreal FirmwareSourceViewModel::designatedProgress() const {
    return m_designatedTotal > 0
               ? static_cast<qreal>(m_designatedDone) / static_cast<qreal>(m_designatedTotal)
               : -1;
}

ErrorInfo FirmwareSourceViewModel::designatedError() const {
    return m_designatedError ? ErrorInfo(*m_designatedError) : ErrorInfo();
}

void FirmwareSourceViewModel::prepareDesignated() {
    if (m_designatedState == DesignatedIdle && designatedAvailable()) {
        startDesignated();
    }
}

void FirmwareSourceViewModel::retryDesignated() {
    if (m_designatedState == DesignatedFailed || m_designatedState == DesignatedIdle) {
        startDesignated();
    }
}

void FirmwareSourceViewModel::cancelDesignated() {
    if (!m_designatedDownload) {
        return;
    }
    const std::unique_ptr<firmware::DesignatedOperation> download = std::move(m_designatedDownload);
    download->disconnect(this);
    download->cancel();
    m_designatedState = DesignatedIdle;
    designatedChanged(); // signal
    sourceChanged();     // signal
}

void FirmwareSourceViewModel::startDesignated() {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    const device::TargetProfile* lte = lteTarget();
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    if (lte == nullptr || !lte->designatedFirmware || catalog == nullptr) {
        return;
    }
    m_designatedError.reset();
    m_designated.reset();
    m_designatedDone = 0;
    m_designatedTotal = 0;
    m_designatedDownload = catalog->downloadDesignated(
        *lte->designatedFirmware, {}, [this](std::uint64_t done, std::uint64_t total) {
            m_designatedDone = done;
            m_designatedTotal = total;
            designatedChanged(); // signal
        });
    connect(m_designatedDownload.get(), &firmware::DesignatedOperation::finished, this,
            &FirmwareSourceViewModel::onDesignatedFinished);
    m_designatedState = DesignatedDownloading;
    designatedChanged(); // signal
    sourceChanged();     // signal
}

void FirmwareSourceViewModel::onDesignatedFinished() {
    if (!m_designatedDownload || !m_designatedDownload->result()) {
        return;
    }
    Result<firmware::DesignatedDownload> result = *m_designatedDownload->result();
    // Deleted later: this runs inside the operation's own signal (Qt takes over the ownership).
    m_designatedDownload.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    if (!result) {
        Error error = std::move(result).error();
        if (error.code == ErrorCode::Cancelled) {
            m_designatedState = DesignatedIdle;
        } else {
            qCInfo(lcGui).noquote()
                << "Designated firmware:" << toQString(idOf(error.code)) << toQString(error.detail);
            if (error.code == ErrorCode::FirmwareChecksumMismatch) {
                error.message = tr("The downloaded file is damaged. Try again.").toStdString();
            } else if (error.code == ErrorCode::NetworkDownloadFailed) {
                error.suggestion += " " + tr("You can also choose your own file.").toStdString();
            }
            m_designatedError = std::move(error);
            m_designatedState = DesignatedFailed;
        }
        designatedChanged(); // signal
        sourceChanged();     // signal
        return;
    }
    for (const Error& warning : result->warnings) {
        qCInfo(lcGui).noquote() << "Designated firmware:" << toQString(warning.message)
                                << toQString(warning.detail);
    }
    m_designated = LoadedSource{.sourceClass = SourceClass::LteBundle,
                                .path = {},
                                .fileBytes = result->fileBytes,
                                .image = std::shared_ptr<const firmware::FirmwareImage>(
                                    result->bundle, &result->bundle->application),
                                .bundle = result->bundle};
    m_designatedState = DesignatedReady;
    designatedChanged(); // signal
    sourceChanged();     // signal
}

// ---- 0021: the catalog page of an ID (ADR-0166) -----------------------------------------------

QVariantMap FirmwareSourceViewModel::catalogInfo() const {
    QVariantMap info{{QStringLiteral("lookupState"), m_lookupState},
                     {QStringLiteral("text"), catalogInfoText()}};
    if (m_info) {
        info.insert(QStringLiteral("name"), toQString(m_info->name));
        info.insert(QStringLiteral("version"), toQString(m_info->version));
        info.insert(QStringLiteral("uploaded"), toQString(m_info->uploaded).left(10));
    }
    return info;
}

QString FirmwareSourceViewModel::catalogInfoText() const {
    if (m_lookupState == QStringLiteral("loading")) {
        return tr("Looking up the firmware…");
    }
    if (m_lookupState == QStringLiteral("unknown")) {
        return tr("Unknown firmware — it can still be flashed.");
    }
    if (m_lookupState == QStringLiteral("found") && m_info) {
        const QString nameVersion =
            QStringLiteral("%1 %2").arg(toQString(m_info->name), toQString(m_info->version));
        return m_info->uploaded.empty()
                   ? nameVersion
                   : tr("%1 · uploaded %2").arg(nameVersion, toQString(m_info->uploaded).left(10));
    }
    return {};
}

void FirmwareSourceViewModel::startLookup(const QString& id) {
    if (id == m_lookupId && m_lookupState != QStringLiteral("none")) {
        return; // this ID is known or being looked up
    }
    cancelLookup();
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    auto parsed = firmware::FirmwareId::parse(toStdString(id));
    if (catalog == nullptr || !parsed) {
        return;
    }
    m_lookupId = id;
    m_lookup = catalog->lookup(*parsed);
    connect(m_lookup.get(), &firmware::LookupOperation::finished, this,
            &FirmwareSourceViewModel::onLookupFinished);
    m_lookupState = QStringLiteral("loading");
    catalogInfoChanged(); // signal
}

void FirmwareSourceViewModel::onLookupFinished() {
    if (!m_lookup || !m_lookup->result()) {
        return;
    }
    const Result<firmware::CatalogInfo> result = *m_lookup->result();
    m_lookup.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    if (result) {
        m_info = std::make_unique<firmware::CatalogInfo>(*result);
        m_lookupState = QStringLiteral("found");
    } else if (result.error().code == ErrorCode::Cancelled) {
        m_lookupState = QStringLiteral("none");
    } else {
        // Not an error (plan 0021 § Errors): the HEX download is authoritative.
        qCInfo(lcGui).noquote() << "Catalog page:" << toQString(idOf(result.error().code))
                                << toQString(result.error().detail);
        m_lookupState = QStringLiteral("unknown");
    }
    catalogInfoChanged(); // signal
    if (m_downloadWaitsForLookup && m_lookupId == m_downloadId) {
        startCatalogDownload(); // review F1: with the page's hash (or without, when unknown)
    }
}

void FirmwareSourceViewModel::cancelLookup() {
    if (m_downloadWaitsForLookup) {
        // Another ID replaced the one being looked up: its pending download never starts
        // unverified (review F1).
        m_downloadWaitsForLookup = false;
        restoreState();
    }
    if (m_lookup) {
        const std::unique_ptr<firmware::LookupOperation> lookup = std::move(m_lookup);
        lookup->disconnect(this);
        lookup->cancel();
    }
    const bool changed = m_lookupState != QStringLiteral("none") || m_info;
    m_info.reset();
    m_lookupId.clear();
    m_lookupState = QStringLiteral("none");
    if (changed) {
        catalogInfoChanged(); // signal
    }
}

void FirmwareSourceViewModel::loadFile(const QUrl& url) {
    if (!url.isLocalFile()) {
        setError(makeError(ErrorCode::UsageInvalidArgument,
                           "not a local file: " + toStdString(url.toString())));
        return;
    }
    loadPath(url.toLocalFile());
}

void FirmwareSourceViewModel::loadPath(const QString& path) {
    if (m_kind == LteFirmware) {
        setLteSource(OwnFile); // 0021: a file (drop, Browse…, Recent) is the own-file source
    }
    cancelDownload();
    m_pendingName = QFileInfo(path).fileName();
    m_generation = m_services.loader().loadFile(this, std::filesystem::path(path.toStdU16String()));
    setState(Loading);
}

bool FirmwareSourceViewModel::checkCatalogId(const QString& text) {
    const QString trimmed = text.trimmed();
    QString message;
    const auto parsed = firmware::FirmwareId::parse(toStdString(trimmed));
    if (!trimmed.isEmpty() && !parsed) {
        message = tr("The ID has 32 characters 0–9 a–f.");
    }
    if (message != m_catalogIdError) {
        m_catalogIdError = message;
        catalogIdErrorChanged(); // signal
    }
    // 0021 (ADR-0166): a complete ID is looked up at once; anything else clears the info.
    if (parsed && catalogAvailable()) {
        startLookup(toQString(parsed->value()));
    } else {
        cancelLookup();
    }
    return message.isEmpty();
}

void FirmwareSourceViewModel::loadCatalogId(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !checkCatalogId(trimmed) || !catalogAvailable()) {
        if (trimmed.isEmpty()) {
            m_catalogIdError = tr("The ID has 32 characters 0–9 a–f.");
            catalogIdErrorChanged(); // signal
        }
        return;
    }
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    auto id = firmware::FirmwareId::parse(toStdString(trimmed));
    if (catalog == nullptr || !id) {
        setError(makeError(ErrorCode::UsageInvalidArgument, "no firmware catalog for this device"));
        return;
    }
    cancelDownload();
    m_generation = 0; // a pending file load is stale now
    m_downloadId = toQString(id->value());
    m_pendingName = tr("Catalog ID %1").arg(m_downloadId);
    m_downloadProgress = -1;
    startLookup(m_downloadId); // e.g. from the recent list (no-op when known)
    setState(Downloading);
    downloadProgressChanged(); // signal
    if (m_lookupState == QStringLiteral("loading") && m_lookupId == m_downloadId) {
        // 0021 review F1: the page's SHA-256 must verify the download (also a cached HEX), so the
        // download waits for the running lookup (capped by CatalogOptions::lookupTimeout).
        m_downloadWaitsForLookup = true;
        return;
    }
    startCatalogDownload();
}

void FirmwareSourceViewModel::startCatalogDownload() {
    m_downloadWaitsForLookup = false;
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    auto id = firmware::FirmwareId::parse(toStdString(m_downloadId));
    if (catalog == nullptr || !id) {
        setError(makeError(ErrorCode::UsageInvalidArgument, "no firmware catalog for this device"));
        return;
    }
    // 0021 (ADR-0166): the catalog page's SHA-256 verifies the download when it is known; an
    // unreadable page (unknown firmware) does not stop it.
    const std::optional<firmware::Sha256> expected =
        m_info && m_lookupId == m_downloadId ? m_info->sha256 : std::nullopt;
    m_download = catalog->download(
        *id, firmware::DownloadOptions{.noCache = false, .expectedSha256 = expected},
        [this](std::uint64_t done, std::uint64_t total) {
            m_downloadProgress =
                total > 0 ? static_cast<qreal>(done) / static_cast<qreal>(total) : -1;
            downloadProgressChanged(); // signal
        });
    connect(m_download.get(), &firmware::CatalogOperation::finished, this,
            &FirmwareSourceViewModel::onDownloadFinished);
}

void FirmwareSourceViewModel::onDownloadFinished() {
    if (!m_download || !m_download->result()) {
        return;
    }
    Result<firmware::CachedFirmware> result = *m_download->result();
    // Deleted later: this runs inside the operation's own signal (Qt takes over the ownership).
    m_download.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    const QString id = m_downloadId;
    if (!result) {
        if (result.error().code == ErrorCode::Cancelled) {
            restoreState();
            return;
        }
        setError(std::move(result).error());
        return;
    }
    for (const Error& warning : result->warnings) {
        qCInfo(lcGui).noquote() << "Firmware download:" << toQString(warning.message)
                                << toQString(warning.detail);
    }
    m_pendingName = catalogName(id);
    setLoaded(LoadedSource{.sourceClass = SourceClass::IntelHex,
                           .path = {},
                           .fileBytes = result->fileBytes,
                           .image = result->image,
                           .bundle = nullptr},
              RecentSourcesModel::catalogPrefix + id);
}

void FirmwareSourceViewModel::onFileLoaded(quint64 generation, const Result<LoadedSource>& result) {
    if (generation != m_generation || m_generation == 0) {
        return; // stale or another requester's
    }
    m_generation = 0;
    if (!result) {
        setError(result.error());
        return;
    }
    const LoadedSource& loaded = *result;
    setLoaded(loaded, QString::fromStdU16String(loaded.path.u16string()));
}

void FirmwareSourceViewModel::setLoaded(LoadedSource loaded, QString recentSource) {
    m_loaded = std::move(loaded);
    m_loadedName = m_pendingName;
    m_recentSource = std::move(recentSource);
    m_error.reset();
    m_allowUicr = false; // never carried over to another image (ADR-0036)
    allowUicrChanged();  // signal
    const bool fits = !mismatch();
    setState(fits ? Ready : Invalid);
    if (fits) {
        m_recent->add(kindId(m_kind), m_recentSource);
    }
}

void FirmwareSourceViewModel::setError(Error error) {
    qCInfo(lcGui).noquote() << "Firmware source:" << toQString(idOf(error.code))
                            << toQString(error.detail);
    m_loaded.reset();
    m_recentSource.clear();
    m_error = std::move(error);
    setState(Invalid);
}

void FirmwareSourceViewModel::restoreState() {
    if (!m_loaded) {
        setState(None);
    } else {
        setState(mismatch() ? Invalid : Ready);
    }
}

void FirmwareSourceViewModel::setState(SourceState state) {
    m_state = state;
    sourceChanged(); // signal
}

void FirmwareSourceViewModel::loadRecent(int row) {
    const QString source = m_recent->sourceAt(row);
    if (source.isEmpty()) {
        return;
    }
    if (source.startsWith(RecentSourcesModel::catalogPrefix)) {
        loadCatalogId(source.mid(RecentSourcesModel::catalogPrefix.size()));
    } else {
        loadPath(source);
    }
}

void FirmwareSourceViewModel::removeRecent(int row) { m_recent->remove(row); }

void FirmwareSourceViewModel::clear() {
    cancelDownload();
    m_generation = 0;
    m_loaded.reset();
    m_recentSource.clear();
    m_error.reset();
    m_allowUicr = false;
    allowUicrChanged(); // signal
    setState(None);
}

void FirmwareSourceViewModel::cancelDownload() {
    if (m_downloadWaitsForLookup) {
        m_downloadWaitsForLookup = false;
        if (m_state == Downloading) {
            restoreState();
        }
        return;
    }
    if (!m_download) {
        return;
    }
    const std::unique_ptr<firmware::CatalogOperation> download = std::move(m_download);
    download->disconnect(this);
    download->cancel();
    if (m_state == Downloading) {
        restoreState();
    }
}

void FirmwareSourceViewModel::acceptSuggestedKind() {
    if (const int suggested = suggestedKind(); suggested >= 0) {
        if (suggested == LteFirmware) {
            setLteSource(OwnFile); // 0021: the file that caused the hint is the LTE card's file
        }
        kindSuggestionAccepted(suggested); // signal
    }
}

const LoadedSource* FirmwareSourceViewModel::readySource() const {
    if (designatedActive()) {
        return m_designatedState == DesignatedReady && m_designated ? &*m_designated : nullptr;
    }
    return m_state == Ready && m_loaded ? &*m_loaded : nullptr;
}

const LoadedSource* FirmwareSourceViewModel::loadedSource() const {
    if (designatedActive()) {
        return m_designated ? &*m_designated : nullptr;
    }
    return m_loaded ? &*m_loaded : nullptr;
}

QString FirmwareSourceViewModel::catalogName(const QString& id) const {
    // 0021: what the catalog page says, else the ID.
    if (m_info && m_lookupId == id) {
        return QStringLiteral("%1 %2").arg(toQString(m_info->name), toQString(m_info->version));
    }
    return tr("Catalog ID %1").arg(id);
}

void FirmwareSourceViewModel::retranslate() {
    // File names are not translated; "Catalog ID <id>" is.
    if (m_loaded && m_recentSource.startsWith(RecentSourcesModel::catalogPrefix)) {
        m_loadedName = catalogName(m_recentSource.mid(RecentSourcesModel::catalogPrefix.size()));
    }
    if (!m_catalogIdError.isEmpty()) {
        m_catalogIdError = tr("The ID has 32 characters 0–9 a–f.");
        catalogIdErrorChanged(); // signal
    }
    m_recent->retranslate();
    kindChanged();        // signal: filters, drop text, docs URL
    sourceChanged();      // signal
    catalogInfoChanged(); // signal
    designatedChanged();  // signal
}

} // namespace hio::gui

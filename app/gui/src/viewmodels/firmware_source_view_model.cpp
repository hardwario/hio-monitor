#include "viewmodels/firmware_source_view_model.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/profile.h"
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
#include <QString>
#include <QStringList>
#include <QUrl>
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
    // The download first, while every member still exists: aborting it must not reach this object.
    if (m_download) {
        m_download->disconnect(this);
        m_download.reset();
    }
}

QString FirmwareSourceViewModel::targetIdFor(Kind kind) {
    return kind == AppFirmware ? QStringLiteral("app") : QStringLiteral("lte");
}

QString FirmwareSourceViewModel::kindId(Kind kind) {
    switch (kind) {
    case AppFirmware:
        return QStringLiteral("app");
    case LteFirmware:
        return QStringLiteral("lte");
    case ModemFirmware:
        return QStringLiteral("modem");
    }
    return {};
}

void FirmwareSourceViewModel::setKind(Kind kind) {
    if (kind == m_kind) {
        return;
    }
    m_kind = kind;
    m_recent->setKind(kindId(kind));
    if (m_kind == ModemFirmware) {
        cancelDownload(); // no catalog for modem firmware
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

void FirmwareSourceViewModel::setHexFallbackKind(Kind kind) {
    if (kind == ModemFirmware || kind == m_hexFallback) {
        return;
    }
    m_hexFallback = kind;
    sourceChanged(); // signal
}

const device::TargetProfile* FirmwareSourceViewModel::target() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return profile != nullptr ? device::findTarget(*profile, toStdString(targetIdFor(m_kind)))
                              : nullptr;
}

QString FirmwareSourceViewModel::sourceName() const {
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
        return tr("%1 · Intel HEX · %2 · %3").arg(m_loadedName, sizeText(loaded.fileBytes), range);
    }
    case SourceClass::LteBundle:
        return tr("%1 · LTE bundle (application + modem firmware) · %2")
            .arg(m_loadedName, sizeText(loaded.fileBytes));
    case SourceClass::ModemZip:
        return tr("%1 · LTE modem firmware · %2").arg(m_loadedName, sizeText(loaded.fileBytes));
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
    switch (m_loaded->sourceClass) {
    case SourceClass::IntelHex:
        if (m_kind == ModemFirmware) {
            return m_hexFallback;
        }
        return std::nullopt;
    case SourceClass::LteBundle:
        return m_kind == LteFirmware ? std::nullopt : std::optional<Kind>(LteFirmware);
    case SourceClass::ModemZip:
        return m_kind == ModemFirmware ? std::nullopt : std::optional<Kind>(ModemFirmware);
    }
    return std::nullopt;
}

QString FirmwareSourceViewModel::kindHint() const {
    if (m_state != Invalid || m_error || !m_loaded) {
        return {};
    }
    const std::optional<Kind> other = mismatch();
    if (!other) {
        return {};
    }
    switch (m_loaded->sourceClass) {
    case SourceClass::IntelHex:
        return *other == LteFirmware
                   ? tr("This is an Intel HEX file, not modem firmware — switch to LTE firmware?")
                   : tr("This is an Intel HEX file, not modem firmware — switch to application "
                        "firmware?");
    case SourceClass::LteBundle:
        return tr("This is an LTE firmware bundle — switch to LTE firmware?");
    case SourceClass::ModemZip:
        return tr("This is LTE modem firmware — switch to LTE modem firmware?");
    }
    return {};
}

int FirmwareSourceViewModel::suggestedKind() const {
    if (m_state != Invalid || m_error) {
        return -1;
    }
    const std::optional<Kind> other = mismatch();
    return other ? static_cast<int>(*other) : -1;
}

ErrorInfo FirmwareSourceViewModel::error() const {
    return m_error ? ErrorInfo(*m_error) : ErrorInfo();
}

bool FirmwareSourceViewModel::catalogAvailable() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return m_kind != ModemFirmware && (profile == nullptr || !profile->firmwareCatalogUrl.empty());
}

QString FirmwareSourceViewModel::catalogUrl() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return profile != nullptr ? toQString(profile->firmwareCatalogUrl) : QString();
}

bool FirmwareSourceViewModel::modemNoteVisible() const {
    return m_kind == ModemFirmware ||
           (m_kind == LteFirmware && m_loaded && m_loaded->sourceClass == SourceClass::LteBundle);
}

QStringList FirmwareSourceViewModel::nameFilters() const {
    switch (m_kind) {
    case AppFirmware:
        return {tr("Intel HEX files (*.hex)"), tr("All files (*)")};
    case LteFirmware:
        return {tr("LTE firmware (*.hex *.zip)"), tr("All files (*)")};
    case ModemFirmware:
        return {tr("Modem firmware (*.zip)"), tr("All files (*)")};
    }
    return {};
}

QString FirmwareSourceViewModel::dropText() const {
    switch (m_kind) {
    case AppFirmware:
        return tr("Drop a .hex file here");
    case LteFirmware:
        return tr("Drop a .hex file or an LTE bundle .zip here");
    case ModemFirmware:
        return tr("Drop a modem firmware .zip (mfw_nrf91…) here");
    }
    return {};
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
    cancelDownload();
    m_pendingName = QFileInfo(path).fileName();
    m_generation = m_services.loader().loadFile(this, std::filesystem::path(path.toStdU16String()));
    setState(Loading);
}

bool FirmwareSourceViewModel::checkCatalogId(const QString& text) {
    const QString trimmed = text.trimmed();
    QString message;
    if (!trimmed.isEmpty() && !firmware::FirmwareId::parse(toStdString(trimmed))) {
        message = tr("The ID has 32 characters 0–9 a–f.");
    }
    if (message != m_catalogIdError) {
        m_catalogIdError = message;
        catalogIdErrorChanged(); // signal
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
    m_download = catalog->download(
        *id, firmware::DownloadOptions{}, [this](std::uint64_t done, std::uint64_t total) {
            m_downloadProgress =
                total > 0 ? static_cast<qreal>(done) / static_cast<qreal>(total) : -1;
            downloadProgressChanged(); // signal
        });
    connect(m_download.get(), &firmware::CatalogOperation::finished, this,
            &FirmwareSourceViewModel::onDownloadFinished);
    setState(Downloading);
    downloadProgressChanged(); // signal
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
    m_pendingName = tr("Catalog ID %1").arg(id);
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
        kindSuggestionAccepted(suggested); // signal
    }
}

const LoadedSource* FirmwareSourceViewModel::readySource() const {
    return m_state == Ready && m_loaded ? &*m_loaded : nullptr;
}

const LoadedSource* FirmwareSourceViewModel::loadedSource() const {
    return m_loaded ? &*m_loaded : nullptr;
}

void FirmwareSourceViewModel::retranslate() {
    // File names are not translated; "Catalog ID <id>" is.
    if (m_loaded && m_recentSource.startsWith(RecentSourcesModel::catalogPrefix)) {
        m_loadedName =
            tr("Catalog ID %1").arg(m_recentSource.mid(RecentSourcesModel::catalogPrefix.size()));
    }
    if (!m_catalogIdError.isEmpty()) {
        m_catalogIdError = tr("The ID has 32 characters 0–9 a–f.");
        catalogIdErrorChanged(); // signal
    }
    m_recent->retranslate();
    kindChanged();   // signal: filters, drop text
    sourceChanged(); // signal
}

} // namespace hio::gui

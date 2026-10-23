#include "hio/firmware/firmware_catalog.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"
#include "hio/firmware/lte_input.h"
#include "http_fetch.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QIODevice>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace hio::firmware {

namespace {

Error downloadFailed(const FirmwareId& id, std::string detail) {
    Error error = makeError(ErrorCode::NetworkDownloadFailed, std::move(detail));
    error.message = QCoreApplication::translate("hio::firmware", "Downloading firmware %1 failed.")
                        .arg(toQString(id.value()))
                        .toStdString();
    return error;
}

Error notFoundInCatalog(const FirmwareId& id, std::string detail) {
    Error error = makeError(ErrorCode::FirmwareNotFoundInCatalog, std::move(detail));
    error.message =
        QCoreApplication::translate("hio::firmware", "Firmware %1 was not found in the catalog.")
            .arg(toQString(id.value()))
            .toStdString();
    return error;
}

Error checksumMismatch(std::string detail) {
    return makeError(ErrorCode::FirmwareChecksumMismatch, std::move(detail));
}

} // namespace

QUrl catalogBaseUrl(std::string_view profileDefault) {
#ifdef HIO_TEST_OVERRIDES
    // Test builds only (CMake option HIO_TEST_OVERRIDES; review R8).
    const QString override = qEnvironmentVariable("HIO_CATALOG_URL");
    if (!override.isEmpty()) {
        return {override};
    }
#endif
    return {toQString(profileDefault)};
}

QUrl designatedUrl(const DesignatedFirmware& firmware) {
#ifdef HIO_TEST_OVERRIDES
    // Test builds only (CMake option HIO_TEST_OVERRIDES; like HIO_CATALOG_URL).
    const QString override = qEnvironmentVariable("HIO_DESIGNATED_URL");
    if (!override.isEmpty()) {
        return {override};
    }
#endif
    return {toQString(firmware.url)};
}

Sha256 designatedSha256(const DesignatedFirmware& firmware) {
#ifdef HIO_TEST_OVERRIDES
    // Test builds only (CMake option HIO_TEST_OVERRIDES; like HIO_CATALOG_URL).
    if (const auto override =
            parseSha256(qEnvironmentVariable("HIO_DESIGNATED_SHA256").toStdString())) {
        return *override;
    }
#endif
    return firmware.sha256;
}

// ---- CatalogOperation ----------------------------------------------------------------------

CatalogOperation::CatalogOperation(Passkey /*key*/, FirmwareCatalog& catalog, FirmwareId id,
                                   DownloadProgressFn progressFn, std::stop_token stop)
    : m_catalog(&catalog), m_id(std::move(id)), m_progressFn(std::move(progressFn)),
      m_stop(std::move(stop)) {}

CatalogOperation::~CatalogOperation() = default;

void CatalogOperation::start(QNetworkAccessManager& network, const QUrl& url) {
    m_fetch = std::make_unique<detail::HttpFetch>(
        network, url,
        detail::FetchLimits{.timeout = m_catalog->options().transferTimeout,
                            .maxBytes = m_catalog->options().maxBodyBytes},
        m_stop);
    QObject::connect(m_fetch.get(), &detail::HttpFetch::progress, this,
                     [this](quint64 done, quint64 total) {
                         if (m_progressFn) {
                             m_progressFn(done, total);
                         }
                         emit progress(done, total);
                     });
    QObject::connect(m_fetch.get(), &detail::HttpFetch::finished, this,
                     &CatalogOperation::onFetched);
}

void CatalogOperation::cancel() {
    if (isFinished()) {
        return;
    }
    if (m_fetch && !m_fetch->isFinished()) {
        m_fetch->cancel(); // finished() follows
        return;
    }
    finish(std::unexpected(makeError(ErrorCode::Cancelled, "firmware download")));
}

void CatalogOperation::onFetched() {
    if (!m_fetch || !m_fetch->outcome() || isFinished()) {
        return;
    }
    const detail::FetchOutcome& outcome = *m_fetch->outcome();
    const std::uint64_t maxBytes = m_catalog->options().maxBodyBytes;
    if (auto error = detail::fetchError(
            outcome, maxBytes,
            [this](std::string detail) { return downloadFailed(m_id, std::move(detail)); },
            [this](std::string detail) { return notFoundInCatalog(m_id, std::move(detail)); })) {
        finish(std::unexpected(std::move(*error)));
        return;
    }
    const QByteArray& body = outcome.body;
    const auto bytes =
        std::as_bytes(std::span(body.constData(), static_cast<std::size_t>(body.size())));
    if (m_expectedSha256 && sha256Of(bytes) != *m_expectedSha256) {
        finish(std::unexpected(
            checksumMismatch(std::format("{}: SHA-256 {}, the catalog page lists {}", outcome.url,
                                         toHex(sha256Of(bytes)), toHex(*m_expectedSha256)))));
        return;
    }
    auto image = parseIntelHex(bytes, "catalog " + m_id.value());
    if (!image) {
        finish(std::unexpected(std::move(image).error()));
        return;
    }
    CachedFirmware firmware{.id = m_id,
                            .image = std::make_shared<const FirmwareImage>(std::move(*image)),
                            .fileBytes = static_cast<std::uint64_t>(body.size()),
                            .fromCache = false,
                            .warnings = {}};
    if (m_catalog->m_cache != nullptr) {
        if (const Status stored = m_catalog->m_cache->store(m_id, bytes); !stored) {
            qCWarning(log::lcFirmware).noquote()
                << "Firmware not cached:" << toQString(stored.error().detail);
            firmware.warnings.push_back(stored.error());
        }
    }
    qCInfo(log::lcFirmware).noquote()
        << "Downloaded firmware" << toQString(m_id.value()) << body.size() << "bytes";
    finish(std::move(firmware));
}

void CatalogOperation::finish(Result<CachedFirmware> result) {
    if (isFinished()) {
        return;
    }
    m_result = std::move(result);
    emit finished();
}

// ---- DesignatedOperation -------------------------------------------------------------------

DesignatedOperation::DesignatedOperation(Passkey /*key*/, FirmwareCatalog& catalog,
                                         const DesignatedFirmware& firmware,
                                         DownloadProgressFn progressFn, std::stop_token stop)
    : m_catalog(&catalog), m_name(std::format("{} {}", firmware.name, firmware.version)),
      m_url(designatedUrl(firmware)), m_sha256(designatedSha256(firmware)), m_kind(firmware.kind),
      m_progressFn(std::move(progressFn)), m_stop(std::move(stop)) {}

DesignatedOperation::~DesignatedOperation() = default;

void DesignatedOperation::start(QNetworkAccessManager& network) {
    m_fetch = std::make_unique<detail::HttpFetch>(
        network, m_url,
        detail::FetchLimits{.timeout = m_catalog->options().transferTimeout,
                            .maxBytes = m_catalog->options().maxBodyBytes,
                            .totalTimeout = m_catalog->options().transferTimeout},
        m_stop);
    QObject::connect(m_fetch.get(), &detail::HttpFetch::progress, this,
                     [this](quint64 done, quint64 total) {
                         if (m_progressFn) {
                             m_progressFn(done, total);
                         }
                         emit progress(done, total);
                     });
    QObject::connect(m_fetch.get(), &detail::HttpFetch::finished, this,
                     &DesignatedOperation::onFetched);
}

void DesignatedOperation::cancel() {
    if (isFinished()) {
        return;
    }
    if (m_fetch && !m_fetch->isFinished()) {
        m_fetch->cancel(); // finished() follows
        return;
    }
    finish(std::unexpected(makeError(ErrorCode::Cancelled, "firmware download")));
}

Result<std::shared_ptr<const LteBundle>>
DesignatedOperation::load(const std::filesystem::path& file) const {
    auto input = classifyLteInput(file);
    if (!input) {
        return std::unexpected(std::move(input).error());
    }
    auto* bundle = std::get_if<LteBundle>(&*input);
    if (m_kind != FirmwareKind::LteBundle || bundle == nullptr) {
        Error error = makeError(ErrorCode::FirmwareInvalidImage,
                                toUtf8(file) + ": not the expected kind of firmware");
        error.message =
            QCoreApplication::translate("hio::firmware", "The downloaded file is not %1.")
                .arg(toQString(m_name))
                .toStdString();
        return std::unexpected(std::move(error));
    }
    return std::make_shared<const LteBundle>(std::move(*bundle));
}

void DesignatedOperation::onFetched() {
    if (!m_fetch || !m_fetch->outcome() || isFinished()) {
        return;
    }
    const detail::FetchOutcome& outcome = *m_fetch->outcome();
    const auto failed = [this](std::string detail) {
        Error error = makeError(ErrorCode::NetworkDownloadFailed, std::move(detail));
        error.message = QCoreApplication::translate("hio::firmware", "Downloading %1 failed.")
                            .arg(toQString(m_name))
                            .toStdString();
        return error;
    };
    if (auto error =
            detail::fetchError(outcome, m_catalog->options().maxBodyBytes, failed, failed)) {
        finish(std::unexpected(std::move(*error)));
        return;
    }
    const QByteArray& body = outcome.body;
    const auto bytes =
        std::as_bytes(std::span(body.constData(), static_cast<std::size_t>(body.size())));
    if (const Sha256 actual = sha256Of(bytes); actual != m_sha256) {
        finish(std::unexpected(checksumMismatch(std::format(
            "{}: SHA-256 {}, expected {}", outcome.url, toHex(actual), toHex(m_sha256)))));
        return;
    }
    std::vector<Error> warnings;
    if (m_catalog->m_cache != nullptr) {
        if (const Status stored = m_catalog->m_cache->storeDesignated(m_sha256, bytes); !stored) {
            qCWarning(log::lcFirmware).noquote()
                << "Firmware not cached:" << toQString(stored.error().detail);
            warnings.push_back(stored.error());
        }
    }
    qCInfo(log::lcFirmware).noquote()
        << "Downloaded" << toQString(m_name) << body.size() << "bytes, SHA-256 verified";
    finishVerified(bytes, false, std::move(warnings));
}

void DesignatedOperation::finishVerified(std::span<const std::byte> bytes, bool fromCache,
                                         std::vector<Error> warnings) {
    // Review F5: classified from exactly the verified bytes, through a private temporary copy
    // (the cache file may be replaced or evicted by another process meanwhile). The bundle keeps
    // its own extraction directory; the copy is removed after load().
    QTemporaryDir temporary;
    const std::filesystem::path path =
        std::filesystem::path(temporary.path().toStdU16String()) / "designated.zip";
    QFile out(toQString(toUtf8(path)));
    const auto size = static_cast<qint64>(bytes.size());
    if (!temporary.isValid() || !out.open(QIODevice::WriteOnly) ||
        out.write(QByteArrayView(bytes.data(), size).data(), size) != size) {
        finish(std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                         toUtf8(path) + ": " + out.errorString().toStdString())));
        return;
    }
    out.close();
    auto bundle = load(path);
    if (!bundle) {
        finish(std::unexpected(std::move(bundle).error()));
        return;
    }
    finish(DesignatedDownload{.bundle = std::move(*bundle),
                              .fileBytes = static_cast<std::uint64_t>(bytes.size()),
                              .fromCache = fromCache,
                              .warnings = std::move(warnings)});
}

void DesignatedOperation::finish(Result<DesignatedDownload> result) {
    if (isFinished()) {
        return;
    }
    m_result = std::move(result);
    emit finished();
}

// ---- LookupOperation -----------------------------------------------------------------------

LookupOperation::LookupOperation(Passkey /*key*/, FirmwareCatalog& catalog, FirmwareId id,
                                 std::stop_token stop)
    : m_catalog(&catalog), m_id(std::move(id)) {
    m_fetch = std::make_unique<detail::HttpFetch>(
        *catalog.m_network, catalog.pageUrlFor(m_id),
        detail::FetchLimits{.timeout = catalog.options().lookupTimeout,
                            .maxBytes = catalog.options().maxPageBytes,
                            .totalTimeout = catalog.options().lookupTimeout},
        std::move(stop));
    QObject::connect(m_fetch.get(), &detail::HttpFetch::finished, this,
                     &LookupOperation::onFetched);
}

LookupOperation::~LookupOperation() = default;

void LookupOperation::cancel() {
    if (isFinished()) {
        return;
    }
    if (m_fetch && !m_fetch->isFinished()) {
        m_fetch->cancel(); // finished() follows
        return;
    }
    finish(std::unexpected(makeError(ErrorCode::Cancelled, "catalog lookup")));
}

void LookupOperation::onFetched() {
    if (!m_fetch || !m_fetch->outcome() || isFinished()) {
        return;
    }
    const detail::FetchOutcome& outcome = *m_fetch->outcome();
    if (auto error = detail::fetchError(
            outcome, m_catalog->options().maxPageBytes,
            [](const std::string& detail) {
                return makeError(ErrorCode::NetworkDownloadFailed, "catalog page " + detail);
            },
            [this](std::string detail) { return notFoundInCatalog(m_id, std::move(detail)); })) {
        finish(std::unexpected(std::move(*error)));
        return;
    }
    auto info = parseCatalogPage(
        std::string_view(outcome.body.constData(), static_cast<std::size_t>(outcome.body.size())));
    if (!info) {
        qCInfo(log::lcFirmware).noquote()
            << "Catalog page not understood:" << toQString(info.error().detail);
        finish(std::unexpected(std::move(info).error()));
        return;
    }
    qCInfo(log::lcFirmware).noquote() << "Catalog page" << toQString(m_id.value()) << "-"
                                      << toQString(info->name) << toQString(info->version);
    finish(std::move(*info));
}

void LookupOperation::finish(Result<CatalogInfo> result) {
    if (isFinished()) {
        return;
    }
    m_result = std::move(result);
    emit finished();
}

// ---- FirmwareCatalog -----------------------------------------------------------------------

FirmwareCatalog::FirmwareCatalog(CatalogOptions options, FirmwareCache* cache, QObject* parent)
    : QObject(parent), m_options(std::move(options)), m_cache(cache),
      m_network(std::make_unique<QNetworkAccessManager>()) {}

FirmwareCatalog::~FirmwareCatalog() = default;

QUrl FirmwareCatalog::urlFor(const FirmwareId& id) const {
    QString base = m_options.baseUrl.toString();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return {base + QLatin1Char('/') + toQString(id.value()) + QStringLiteral("/hex")};
}

QUrl FirmwareCatalog::pageUrlFor(const FirmwareId& id) const {
    QString base = m_options.baseUrl.toString();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return {base + QLatin1Char('/') + toQString(id.value())};
}

std::unique_ptr<CatalogOperation> FirmwareCatalog::download(const FirmwareId& id,
                                                            DownloadOptions options,
                                                            DownloadProgressFn progress,
                                                            std::stop_token stop) {
    auto operation = std::make_unique<CatalogOperation>(CatalogOperation::Passkey{}, *this, id,
                                                        std::move(progress), std::move(stop));
    operation->m_expectedSha256 = options.expectedSha256;
    if (!options.noCache && m_cache != nullptr) {
        auto hit = m_cache->lookup(id);
        if (hit && options.expectedSha256 && hit->fileSha256 != *options.expectedSha256) {
            // The catalog lists another file for this ID: the cached one is not used.
            qCWarning(log::lcFirmware).noquote()
                << "Cached firmware differs from the catalog page, downloading again:"
                << toQString(id.value());
            std::error_code ec;
            std::filesystem::remove(hit->file, ec);
            hit.reset();
        }
        if (hit) {
            CachedFirmware firmware{
                .id = id,
                .image = std::make_shared<const FirmwareImage>(std::move(hit->image)),
                .fileBytes = hit->fileBytes,
                .fromCache = true,
                .warnings = {}};
            // Reported asynchronously, like a download.
            QTimer::singleShot(std::chrono::milliseconds(0), operation.get(),
                               [op = operation.get(), firmware = std::move(firmware)]() mutable {
                                   op->finish(std::move(firmware));
                               });
            return operation;
        }
    }
    operation->start(*m_network, urlFor(id));
    return operation;
}

std::unique_ptr<DesignatedOperation>
FirmwareCatalog::downloadDesignated(const DesignatedFirmware& firmware, DownloadOptions options,
                                    DownloadProgressFn progress, std::stop_token stop) {
    auto operation = std::make_unique<DesignatedOperation>(
        DesignatedOperation::Passkey{}, *this, firmware, std::move(progress), std::move(stop));
    if (!options.noCache && m_cache != nullptr) {
        if (auto verified = m_cache->lookupDesignated(operation->m_sha256)) {
            // Classified from the verified bytes and reported asynchronously, like a download.
            QTimer::singleShot(std::chrono::milliseconds(0), operation.get(),
                               [op = operation.get(), bytes = std::move(*verified)] {
                                   op->finishVerified(bytes, true, {});
                               });
            return operation;
        }
    }
    operation->start(*m_network);
    return operation;
}

std::unique_ptr<LookupOperation> FirmwareCatalog::lookup(const FirmwareId& id,
                                                         std::stop_token stop) {
    return std::make_unique<LookupOperation>(LookupOperation::Passkey{}, *this, id,
                                             std::move(stop));
}

Result<CachedFirmware> waitForDownload(CatalogOperation& operation) {
    if (!operation.isFinished()) {
        QEventLoop loop;
        QObject::connect(&operation, &CatalogOperation::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (const auto& result = operation.result()) {
        return *result;
    }
    return std::unexpected(makeError(ErrorCode::InternalUnexpected, "download did not finish"));
}

namespace {

template <class Operation, class Value>
Result<Value> waitFor(Operation& operation, const char* what) {
    if (!operation.isFinished()) {
        QEventLoop loop;
        QObject::connect(&operation, &Operation::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (const auto& result = operation.result()) {
        return *result;
    }
    return std::unexpected(makeError(ErrorCode::InternalUnexpected, what));
}

} // namespace

Result<DesignatedDownload> waitForDownload(DesignatedOperation& operation) {
    return waitFor<DesignatedOperation, DesignatedDownload>(operation, "download did not finish");
}

Result<CatalogInfo> waitForLookup(LookupOperation& operation) {
    return waitFor<LookupOperation, CatalogInfo>(operation, "lookup did not finish");
}

} // namespace hio::firmware

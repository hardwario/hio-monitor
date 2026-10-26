#include "hio/firmware/firmware_catalog.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace hio::firmware {

namespace {

constexpr std::chrono::milliseconds stopPollInterval{50};

Error downloadFailed(const FirmwareId& id, std::string detail) {
    Error error = makeError(ErrorCode::NetworkDownloadFailed, std::move(detail));
    error.message = QCoreApplication::translate("hio::firmware", "Downloading firmware %1 failed.")
                        .arg(toQString(id.value()))
                        .toStdString();
    return error;
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

// ---- CatalogOperation ----------------------------------------------------------------------

CatalogOperation::CatalogOperation(Passkey /*key*/, FirmwareCatalog& catalog, FirmwareId id,
                                   DownloadProgressFn progressFn, std::stop_token stop)
    : m_catalog(&catalog), m_id(std::move(id)), m_progressFn(std::move(progressFn)),
      m_stop(std::move(stop)) {
    m_stopPoll.setInterval(stopPollInterval);
    QObject::connect(&m_stopPoll, &QTimer::timeout, this, [this] {
        if (m_stop.stop_requested()) {
            cancel();
        }
    });
    if (m_stop.stop_possible()) {
        m_stopPoll.start();
    }
}

CatalogOperation::~CatalogOperation() {
    if (!m_reply.isNull()) {
        QObject::disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void CatalogOperation::start(QNetworkAccessManager& network, const QUrl& url) {
    QNetworkRequest request(url);
    request.setTransferTimeout(m_catalog->options().transferTimeout);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);
    qCInfo(log::lcFirmware).noquote() << "Downloading" << url.toString();
    m_reply = network.get(request);
    // Redirects are followed only to https (review R8), also from an http test base URL.
    QObject::connect(m_reply, &QNetworkReply::redirected, this, [this](const QUrl& to) {
        if (to.scheme() == QStringLiteral("https")) {
            qCInfo(log::lcFirmware).noquote() << "Redirected to" << to.toString();
            emit m_reply->redirectAllowed();
        } else {
            qCWarning(log::lcFirmware).noquote()
                << "Redirect refused (not https):" << to.toString();
            m_redirectRefused = true;
            m_reply->abort();
        }
    });
    QObject::connect(m_reply, &QNetworkReply::readyRead, this, &CatalogOperation::onReadyRead);
    QObject::connect(
        m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            const auto done = static_cast<std::uint64_t>(std::max<qint64>(received, 0));
            const auto all = static_cast<std::uint64_t>(std::max<qint64>(total, 0));
            if (m_progressFn) {
                m_progressFn(done, all);
            }
            emit progress(done, all);
        });
    QObject::connect(m_reply, &QNetworkReply::finished, this, &CatalogOperation::onFinished);
}

void CatalogOperation::onReadyRead() {
    if (m_reply.isNull()) {
        return;
    }
    const QByteArray chunk = m_reply->readAll();
    if (static_cast<std::uint64_t>(m_body.size()) + static_cast<std::uint64_t>(chunk.size()) >
        m_catalog->options().maxBodyBytes) {
        m_oversize = true;
        m_reply->abort();
        return;
    }
    m_body.append(chunk);
}

void CatalogOperation::cancel() {
    if (isFinished()) {
        return;
    }
    m_cancelled = true;
    if (!m_reply.isNull()) {
        m_reply->abort(); // finished() follows
        return;
    }
    finish(std::unexpected(makeError(ErrorCode::Cancelled, "firmware download")));
}

void CatalogOperation::onFinished() {
    if (m_reply.isNull() || isFinished()) {
        return;
    }
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    if (!m_cancelled && !m_oversize) {
        onReadyRead(); // the rest of the body
    }
    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const std::string url = reply->url().toString().toStdString();
    if (m_cancelled) {
        finish(std::unexpected(makeError(ErrorCode::Cancelled, "firmware download " + url)));
        return;
    }
    if (m_oversize) {
        finish(std::unexpected(
            downloadFailed(m_id, std::format("{}: the response is larger than {} bytes", url,
                                             m_catalog->options().maxBodyBytes))));
        return;
    }
    if (status == 404) {
        Error error = makeError(ErrorCode::FirmwareNotFoundInCatalog, url + ": HTTP 404");
        error.message = QCoreApplication::translate("hio::firmware",
                                                    "Firmware %1 was not found in the catalog.")
                            .arg(toQString(m_id.value()))
                            .toStdString();
        finish(std::unexpected(std::move(error)));
        return;
    }
    if (m_redirectRefused) {
        finish(
            std::unexpected(downloadFailed(m_id, url + ": redirect to a non-https URL refused")));
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        // Our own aborts are handled above, so a cancelled transfer is the transfer timeout.
        const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError ||
                              reply->error() == QNetworkReply::TimeoutError;
        finish(std::unexpected(downloadFailed(
            m_id, std::format("{}: HTTP {}, {}{}", url, status, reply->errorString().toStdString(),
                              timedOut ? " (transfer timeout)" : ""))));
        return;
    }
    const auto bytes =
        std::as_bytes(std::span(m_body.constData(), static_cast<std::size_t>(m_body.size())));
    auto image = parseIntelHex(bytes, "catalog " + m_id.value());
    if (!image) {
        finish(std::unexpected(std::move(image).error()));
        return;
    }
    CachedFirmware firmware{.id = m_id,
                            .image = std::make_shared<const FirmwareImage>(std::move(*image)),
                            .fileBytes = static_cast<std::uint64_t>(m_body.size()),
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
        << "Downloaded firmware" << toQString(m_id.value()) << m_body.size() << "bytes";
    finish(std::move(firmware));
}

void CatalogOperation::finish(Result<CachedFirmware> result) {
    if (isFinished()) {
        return;
    }
    m_stopPoll.stop();
    m_body.clear();
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

std::unique_ptr<CatalogOperation> FirmwareCatalog::download(const FirmwareId& id,
                                                            DownloadOptions options,
                                                            DownloadProgressFn progress,
                                                            std::stop_token stop) {
    auto operation = std::make_unique<CatalogOperation>(CatalogOperation::Passkey{}, *this, id,
                                                        std::move(progress), std::move(stop));
    if (!options.noCache && m_cache != nullptr) {
        if (auto hit = m_cache->lookup(id)) {
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

} // namespace hio::firmware

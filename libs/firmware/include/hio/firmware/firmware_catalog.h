#pragma once

// Firmware download from the HARDWARIO catalog by ID (ADR-0037, plan/features/0003).
//
// URL: <base>/<id>/hex (base from the device profile, e.g. https://firmware.hardwario.com/chester;
// the environment variable HIO_CATALOG_URL replaces it - test-only override). QtNetwork on the
// owner thread (GUI main thread / CLI QCoreApplication thread), asynchronous and cancellable:
//   - transfer timeout (no data for transferTimeout) -> network.download_failed
//   - redirects are followed only to https (or, for an http base used by tests, to the base's
//     scheme)
//   - HTTP 404 -> firmware.not_found_in_catalog; other non-2xx and network errors ->
//     network.download_failed (HTTP status and the Qt error in `detail`)
//   - the body is capped at maxBodyBytes (network.download_failed)
//   - the body must parse as Intel HEX before it is cached or used (firmware.hex_parse_error)
//   - cancel / stop request -> cancelled
// The cache (FirmwareCache) is used unless DownloadOptions::noCache; a download is always stored.
//
// Thread safety: FirmwareCatalog, its operations and the cache belong to the owner thread.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_image.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace hio::firmware {

struct CatalogOptions {
    QUrl baseUrl;
    std::chrono::milliseconds transferTimeout{60000};
    std::uint64_t maxBodyBytes = 32ULL * 1024 * 1024;
};

// `profileDefault` unless HIO_CATALOG_URL is set (test-only override).
[[nodiscard]] QUrl catalogBaseUrl(std::string_view profileDefault);

struct DownloadOptions {
    bool noCache = false; // skip the cache lookup (the download is still stored)
};

struct CachedFirmware {
    std::optional<FirmwareId> id;
    std::shared_ptr<const FirmwareImage> image;
    std::uint64_t fileBytes = 0; // size of the HEX text
    bool fromCache = false;
    std::vector<Error> warnings; // e.g. io.write_failed when the cache could not be written
};

using DownloadProgressFn = std::function<void(std::uint64_t done, std::uint64_t total)>;

class FirmwareCatalog;

// One download. Emits progress() and, once, finished(); result() is set before finished().
class CatalogOperation : public QObject {
    Q_OBJECT

    struct Passkey {
        explicit Passkey() = default;
    };

public:
    CatalogOperation(Passkey key, FirmwareCatalog& catalog, FirmwareId id,
                     DownloadProgressFn progressFn, std::stop_token stop);
    ~CatalogOperation() override; // aborts a running transfer
    CatalogOperation(const CatalogOperation&) = delete;
    CatalogOperation& operator=(const CatalogOperation&) = delete;
    CatalogOperation(CatalogOperation&&) = delete;
    CatalogOperation& operator=(CatalogOperation&&) = delete;

    [[nodiscard]] bool isFinished() const noexcept { return m_result.has_value(); }
    [[nodiscard]] const std::optional<Result<CachedFirmware>>& result() const noexcept {
        return m_result;
    }
    // Ends the download with `cancelled` (no-op when finished).
    void cancel();

signals:
    void progress(quint64 done, quint64 total);
    void finished();

private:
    friend class FirmwareCatalog;

    void start(QNetworkAccessManager& network, const QUrl& url);
    void onReadyRead();
    void onFinished();
    void finish(Result<CachedFirmware> result);

    FirmwareCatalog* m_catalog; // non-owning; the catalog outlives its operations
    FirmwareId m_id;
    DownloadProgressFn m_progressFn;
    std::stop_token m_stop;
    QTimer m_stopPoll;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_body;
    bool m_cancelled = false;
    bool m_oversize = false;
    bool m_redirectRefused = false;
    std::optional<Result<CachedFirmware>> m_result;
};

class FirmwareCatalog : public QObject {
    Q_OBJECT

public:
    // `cache` may be nullptr (no caching); it must outlive the catalog.
    FirmwareCatalog(CatalogOptions options, FirmwareCache* cache, QObject* parent = nullptr);
    ~FirmwareCatalog() override;
    FirmwareCatalog(const FirmwareCatalog&) = delete;
    FirmwareCatalog& operator=(const FirmwareCatalog&) = delete;
    FirmwareCatalog(FirmwareCatalog&&) = delete;
    FirmwareCatalog& operator=(FirmwareCatalog&&) = delete;

    [[nodiscard]] const CatalogOptions& options() const noexcept { return m_options; }
    [[nodiscard]] QUrl urlFor(const FirmwareId& id) const;

    // Starts a download (or a cache lookup, reported asynchronously). The operation must not
    // outlive the catalog.
    [[nodiscard]] std::unique_ptr<CatalogOperation> download(const FirmwareId& id,
                                                             DownloadOptions options,
                                                             DownloadProgressFn progress = {},
                                                             std::stop_token stop = {});

private:
    friend class CatalogOperation;

    CatalogOptions m_options;
    FirmwareCache* m_cache; // non-owning, may be nullptr
    std::unique_ptr<QNetworkAccessManager> m_network;
};

// Runs a local event loop until `operation` finished and returns its result (CLI helper).
[[nodiscard]] Result<CachedFirmware> waitForDownload(CatalogOperation& operation);

} // namespace hio::firmware

#pragma once

// Firmware download from the HARDWARIO catalog by ID (ADR-0037, plan/features/0003).
//
// URL: <base>/<id>/hex (base from the device profile, e.g. https://firmware.hardwario.com/chester;
// in builds with the CMake option HIO_TEST_OVERRIDES only, the environment variable
// HIO_CATALOG_URL replaces it - test-only override, review R8). QtNetwork on the
// owner thread (GUI main thread / CLI QCoreApplication thread), asynchronous and cancellable:
//   - transfer timeout (no data for transferTimeout) -> network.download_failed
//   - redirects are followed only to https
//   - HTTP 404 -> firmware.not_found_in_catalog; other non-2xx and network errors ->
//     network.download_failed (HTTP status and the Qt error in `detail`)
//   - the body is capped at maxBodyBytes (network.download_failed)
//   - the body must parse as Intel HEX before it is cached or used (firmware.hex_parse_error)
//   - cancel / stop request -> cancelled
// The cache (FirmwareCache) is used unless DownloadOptions::noCache; a download is always stored.
//
// Plan 0021 (ADR-0165, ADR-0166):
//   - downloadDesignated(): the designated firmware of a target (profile entry), with the same
//     rules; the SHA-256 is verified before the file is cached or used (firmware.checksum_mismatch,
//     the file is discarded); cache hits (lte-<sha256>.zip) are re-verified; the file must classify
//     as the designated kind (ADR-0040; LTE bundle: firmware.invalid_image otherwise).
//   - lookup(): the public catalog page <base>/<id> (lookupTimeout, maxPageBytes) parsed by
//     parseCatalogPage() into name, version, upload time and the SHA-256 of the HEX.
//   - download() with DownloadOptions::expectedSha256 (the lookup's hash): the HEX text must have
//     it (firmware.checksum_mismatch, not cached); a cached entry with another hash is deleted and
//     downloaded again.
//
// Thread safety: FirmwareCatalog, its operations and the cache belong to the owner thread.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

class QNetworkAccessManager;

namespace hio::firmware {

namespace detail {
class HttpFetch;
} // namespace detail

struct CatalogOptions {
    QUrl baseUrl;
    std::chrono::milliseconds transferTimeout{60000};
    std::uint64_t maxBodyBytes = 32ULL * 1024 * 1024;
    // Plan 0021: the catalog page of lookup().
    std::chrono::milliseconds lookupTimeout{15000};
    std::uint64_t maxPageBytes = 1024ULL * 1024;
};

// `profileDefault`, or HIO_CATALOG_URL in HIO_TEST_OVERRIDES builds (test-only override).
[[nodiscard]] QUrl catalogBaseUrl(std::string_view profileDefault);

// The URL / SHA-256 of a designated firmware: the profile's, or HIO_DESIGNATED_URL /
// HIO_DESIGNATED_SHA256 in HIO_TEST_OVERRIDES builds (test-only overrides, like HIO_CATALOG_URL).
[[nodiscard]] QUrl designatedUrl(const DesignatedFirmware& firmware);
[[nodiscard]] Sha256 designatedSha256(const DesignatedFirmware& firmware);

struct DownloadOptions {
    bool noCache = false; // skip the cache lookup (the download is still stored)
    // Plan 0021 (ADR-0166): the SHA-256 of the HEX text from the catalog page (lookup()).
    std::optional<Sha256> expectedSha256 = std::nullopt;
};

// What the public catalog page says about a firmware (ADR-0166).
struct CatalogInfo {
    std::string name;             // "hio-chester-push-fm"
    std::string version;          // "v4.0.1" (as on the page)
    std::string uploaded;         // "2026-09-07 12:13:53" ("" when not on the page)
    std::optional<Sha256> sha256; // of the HEX file (nullopt when not on the page)
};

// Limits of parseCatalogPage().
inline constexpr std::size_t maxCatalogFieldBytes = 128;

// Parses the HTML of the catalog page (pure, bounded, tolerant: text outside the expected marks
// is ignored). The name and version come from the "<name>:<version>" heading; the HEX SHA-256 from
// the first "SHA256 checksum:" after the HEX download; the upload time from "Upload date/time".
// Errors: network.download_failed (no "<name>:<version>" on the page; the reason in `detail`).
[[nodiscard]] std::expected<CatalogInfo, Error> parseCatalogPage(std::string_view html);

// The result of downloadDesignated().
struct DesignatedDownload {
    std::shared_ptr<const LteBundle> bundle;
    std::uint64_t fileBytes = 0;
    bool fromCache = false;
    std::vector<Error> warnings; // e.g. io.write_failed when the cache could not be written
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
    void onFetched();
    void finish(Result<CachedFirmware> result);

    FirmwareCatalog* m_catalog; // non-owning; the catalog outlives its operations
    FirmwareId m_id;
    DownloadProgressFn m_progressFn;
    std::stop_token m_stop;
    std::optional<Sha256> m_expectedSha256;
    std::unique_ptr<detail::HttpFetch> m_fetch;
    std::optional<Result<CachedFirmware>> m_result;
};

// One designated firmware download (plan 0021). Emits progress() and, once, finished().
class DesignatedOperation : public QObject {
    Q_OBJECT

    struct Passkey {
        explicit Passkey() = default;
    };

public:
    DesignatedOperation(Passkey key, FirmwareCatalog& catalog, const DesignatedFirmware& firmware,
                        DownloadProgressFn progressFn, std::stop_token stop);
    ~DesignatedOperation() override;
    DesignatedOperation(const DesignatedOperation&) = delete;
    DesignatedOperation& operator=(const DesignatedOperation&) = delete;
    DesignatedOperation(DesignatedOperation&&) = delete;
    DesignatedOperation& operator=(DesignatedOperation&&) = delete;

    [[nodiscard]] bool isFinished() const noexcept { return m_result.has_value(); }
    [[nodiscard]] const std::optional<Result<DesignatedDownload>>& result() const noexcept {
        return m_result;
    }
    void cancel();

signals:
    void progress(quint64 done, quint64 total);
    void finished();

private:
    friend class FirmwareCatalog;

    void start(QNetworkAccessManager& network);
    void onFetched();
    void finish(Result<DesignatedDownload> result);
    // Classifies the verified bytes and finishes (review F5).
    void finishVerified(std::span<const std::byte> bytes, bool fromCache,
                        std::vector<Error> warnings);
    // Classifies `file` (must be the designated kind).
    [[nodiscard]] Result<std::shared_ptr<const LteBundle>>
    load(const std::filesystem::path& file) const;

    FirmwareCatalog* m_catalog; // non-owning; the catalog outlives its operations
    std::string m_name;         // "HARDWARIO LTE firmware 1.7.0" (messages)
    QUrl m_url;
    Sha256 m_sha256{};
    FirmwareKind m_kind;
    DownloadProgressFn m_progressFn;
    std::stop_token m_stop;
    std::unique_ptr<detail::HttpFetch> m_fetch;
    std::optional<Result<DesignatedDownload>> m_result;
};

// One catalog page lookup (plan 0021). Emits finished() once.
class LookupOperation : public QObject {
    Q_OBJECT

    struct Passkey {
        explicit Passkey() = default;
    };

public:
    LookupOperation(Passkey key, FirmwareCatalog& catalog, FirmwareId id, std::stop_token stop);
    ~LookupOperation() override;
    LookupOperation(const LookupOperation&) = delete;
    LookupOperation& operator=(const LookupOperation&) = delete;
    LookupOperation(LookupOperation&&) = delete;
    LookupOperation& operator=(LookupOperation&&) = delete;

    [[nodiscard]] const FirmwareId& id() const noexcept { return m_id; }
    [[nodiscard]] bool isFinished() const noexcept { return m_result.has_value(); }
    [[nodiscard]] const std::optional<Result<CatalogInfo>>& result() const noexcept {
        return m_result;
    }
    void cancel();

signals:
    void finished();

private:
    friend class FirmwareCatalog;

    void onFetched();
    void finish(Result<CatalogInfo> result);

    FirmwareCatalog* m_catalog; // non-owning; the catalog outlives its operations
    FirmwareId m_id;
    std::unique_ptr<detail::HttpFetch> m_fetch;
    std::optional<Result<CatalogInfo>> m_result;
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
    // The public page of a firmware: <base>/<id> (plan 0021).
    [[nodiscard]] QUrl pageUrlFor(const FirmwareId& id) const;

    // Starts a download (or a cache lookup, reported asynchronously). The operation must not
    // outlive the catalog.
    [[nodiscard]] std::unique_ptr<CatalogOperation> download(const FirmwareId& id,
                                                             DownloadOptions options,
                                                             DownloadProgressFn progress = {},
                                                             std::stop_token stop = {});

    // Plan 0021 (ADR-0165): downloads (or takes from the cache) and verifies the designated
    // firmware. DownloadOptions::noCache skips the cache lookup; expectedSha256 is not used (the
    // profile's hash is). The operation must not outlive the catalog.
    [[nodiscard]] std::unique_ptr<DesignatedOperation>
    downloadDesignated(const DesignatedFirmware& firmware, DownloadOptions options = {},
                       DownloadProgressFn progress = {}, std::stop_token stop = {});

    // Plan 0021 (ADR-0166): reads the catalog page of `id`. Errors: firmware.not_found_in_catalog
    // (HTTP 404), network.download_failed, cancelled.
    [[nodiscard]] std::unique_ptr<LookupOperation> lookup(const FirmwareId& id,
                                                          std::stop_token stop = {});

private:
    friend class CatalogOperation;
    friend class DesignatedOperation;
    friend class LookupOperation;

    CatalogOptions m_options;
    FirmwareCache* m_cache; // non-owning, may be nullptr
    std::unique_ptr<QNetworkAccessManager> m_network;
};

// Runs a local event loop until `operation` finished and returns its result (CLI helper).
[[nodiscard]] Result<CachedFirmware> waitForDownload(CatalogOperation& operation);
[[nodiscard]] Result<DesignatedDownload> waitForDownload(DesignatedOperation& operation);
[[nodiscard]] Result<CatalogInfo> waitForLookup(LookupOperation& operation);

} // namespace hio::firmware

#pragma once

// UpdateCheck - one request for the GitHub release list (plan/features/0016, ADR-0116).
//
// Pattern of libs/firmware CatalogOperation (ADR-0037): an owner-thread operation that uses a
// QNetworkAccessManager owned by the caller; asynchronous (no blocking, no nested event loop).
//   - request: GET `url` with Accept: application/vnd.github+json, X-GitHub-Api-Version:
//     2022-11-28 and User-Agent: hio-monitor/<version>; no authentication, no cookies, no other
//     data;
//   - bounds: response <= maxResponseBytes (aborted when exceeded: update.too_large), transfer
//     timeout 15 s (no data for that long: update.network), redirects only to the same origin
//     (QNetworkRequest::SameOriginRedirectPolicy; others: update.network);
//   - HTTP 403/429 with rate-limit headers (X-RateLimit-Remaining: 0 or Retry-After):
//     update.rate_limited (the reset time from X-RateLimit-Reset in `detail`); other HTTP errors
//     and unreadable bodies: update.bad_response; no connection: update.network;
//   - cancel(): finished with `cancelled`.
// finished() is emitted exactly once per start(), asynchronously (never from inside start()).
//
// Thread safety: owner thread only.

#include "hio/core/result.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"

#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <chrono>
#include <optional>

class QNetworkAccessManager;

namespace hio::update {

inline constexpr std::chrono::milliseconds transferTimeout{15000};

class UpdateCheck : public QObject {
    Q_OBJECT

public:
    explicit UpdateCheck(QObject* parent = nullptr);
    ~UpdateCheck() override; // aborts a running request (finished() is not emitted)
    UpdateCheck(const UpdateCheck&) = delete;
    UpdateCheck& operator=(const UpdateCheck&) = delete;
    UpdateCheck(UpdateCheck&&) = delete;
    UpdateCheck& operator=(UpdateCheck&&) = delete;

    // Starts one request; `network` must outlive the request. A running request is cancelled
    // first (its finished() is emitted with `cancelled`).
    void start(QNetworkAccessManager& network, const QUrl& url, const SemVer& current,
               bool includePrerelease);
    // Ends a running request with `cancelled` (no-op when none runs).
    void cancel();

    [[nodiscard]] bool isRunning() const noexcept { return !m_reply.isNull(); }
    // The result of the last finished request.
    [[nodiscard]] const std::optional<Result<CheckResult>>& result() const noexcept {
        return m_result;
    }

    // Test seam: the transfer timeout (default transferTimeout).
    void setTransferTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }

signals:
    void finished(const hio::Result<hio::update::CheckResult>& result);

private:
    void onReadyRead();
    void onFinished();
    void finish(Result<CheckResult> result);

    QPointer<QNetworkReply> m_reply;
    QByteArray m_body;
    SemVer m_current;
    bool m_includePrerelease = false;
    bool m_cancelled = false;
    bool m_oversize = false;
    std::chrono::milliseconds m_timeout = transferTimeout;
    std::optional<Result<CheckResult>> m_result;
};

// Runs a local event loop until `check` finished and returns its result (CLI helper; `check` must
// have been started).
[[nodiscard]] Result<CheckResult> waitForCheck(UpdateCheck& check);

} // namespace hio::update

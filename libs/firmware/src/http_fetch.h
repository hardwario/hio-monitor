#pragma once

// One bounded HTTP GET on the owner thread (ADR-0037), shared by the catalog download, the
// designated firmware download and the catalog page lookup (plan/features/0021):
//   - transfer timeout (no data for `timeout`), optional total time limit (`totalTimeout`),
//     redirects followed only to https;
//   - the body is capped at `maxBytes` (the transfer is aborted beyond it);
//   - cancel() / a stop request abort the transfer;
//   - finished() is emitted once; outcome() is set before it.
// fetchError() turns an outcome into the error of the operation (nullopt for a usable 2xx body).
//
// Thread safety: owner thread only (QNetworkAccessManager).

#include "hio/core/error.h"

#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>

class QNetworkAccessManager;

namespace hio::firmware::detail {

struct FetchLimits {
    std::chrono::milliseconds timeout{60000}; // inactivity (no data for this long)
    std::uint64_t maxBytes = 32ULL * 1024 * 1024;
    // Review F9: the whole transfer at most this long (0 = no total limit).
    std::chrono::milliseconds totalTimeout{0};
};

struct FetchOutcome {
    std::string url; // the requested URL (for error details)
    int status = 0;  // HTTP status (0 = none)
    QByteArray body; // complete only for a 2xx status without error
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    std::string errorString;
    bool cancelled = false;
    bool oversize = false;
    bool redirectRefused = false;
    bool totalTimeout = false; // the FetchLimits::totalTimeout ran out
};

class HttpFetch : public QObject {
    Q_OBJECT

public:
    // Starts the request at once. `network` must outlive the fetch.
    HttpFetch(QNetworkAccessManager& network, const QUrl& url, FetchLimits limits,
              std::stop_token stop);
    ~HttpFetch() override; // aborts a running transfer
    HttpFetch(const HttpFetch&) = delete;
    HttpFetch& operator=(const HttpFetch&) = delete;
    HttpFetch(HttpFetch&&) = delete;
    HttpFetch& operator=(HttpFetch&&) = delete;

    [[nodiscard]] bool isFinished() const noexcept { return m_outcome.has_value(); }
    [[nodiscard]] const std::optional<FetchOutcome>& outcome() const noexcept { return m_outcome; }
    // Ends the fetch with `cancelled` (no-op when finished).
    void cancel();

signals:
    void progress(quint64 done, quint64 total);
    void finished();

private:
    void onReadyRead();
    void onFinished();

    FetchLimits m_limits;
    std::stop_token m_stop;
    QTimer m_stopPoll;
    QTimer m_totalTimer; // single shot, FetchLimits::totalTimeout
    QPointer<QNetworkReply> m_reply;
    QByteArray m_body;
    std::string m_url;
    bool m_cancelled = false;
    bool m_oversize = false;
    bool m_redirectRefused = false;
    bool m_totalTimeout = false;
    std::optional<FetchOutcome> m_outcome;
};

// The error of a finished fetch, nullopt when the body is usable (2xx, complete):
//   cancelled -> cancelled; HTTP 404 -> notFound(detail); everything else -> failed(detail).
[[nodiscard]] std::optional<Error>
fetchError(const FetchOutcome& outcome, std::uint64_t maxBytes,
           const std::function<Error(std::string detail)>& failed,
           const std::function<Error(std::string detail)>& notFound);

} // namespace hio::firmware::detail

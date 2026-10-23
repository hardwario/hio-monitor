#include "http_fetch.h"

#include "hio/core/error.h"
#include "hio/core/log.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace hio::firmware::detail {

namespace {

constexpr std::chrono::milliseconds stopPollInterval{50};

} // namespace

HttpFetch::HttpFetch(QNetworkAccessManager& network, const QUrl& url, FetchLimits limits,
                     std::stop_token stop)
    : m_limits(limits), m_stop(std::move(stop)), m_url(url.toString().toStdString()) {
    m_stopPoll.setInterval(stopPollInterval);
    QObject::connect(&m_stopPoll, &QTimer::timeout, this, [this] {
        if (m_stop.stop_requested()) {
            cancel();
        }
    });
    if (m_stop.stop_possible()) {
        m_stopPoll.start();
    }
    if (m_limits.totalTimeout.count() > 0) {
        m_totalTimer.setSingleShot(true);
        m_totalTimer.setInterval(m_limits.totalTimeout);
        QObject::connect(&m_totalTimer, &QTimer::timeout, this, [this] {
            if (m_reply && !isFinished()) {
                m_totalTimeout = true;
                m_reply->abort(); // finished() follows
            }
        });
        m_totalTimer.start();
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(m_limits.timeout);
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
    QObject::connect(m_reply, &QNetworkReply::readyRead, this, &HttpFetch::onReadyRead);
    QObject::connect(m_reply, &QNetworkReply::downloadProgress, this,
                     [this](qint64 received, qint64 total) {
                         emit progress(static_cast<quint64>(std::max<qint64>(received, 0)),
                                       static_cast<quint64>(std::max<qint64>(total, 0)));
                     });
    QObject::connect(m_reply, &QNetworkReply::finished, this, &HttpFetch::onFinished);
}

HttpFetch::~HttpFetch() {
    if (m_reply) {
        QObject::disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void HttpFetch::onReadyRead() {
    if (!m_reply) {
        return;
    }
    const QByteArray chunk = m_reply->readAll();
    if (static_cast<std::uint64_t>(m_body.size()) + static_cast<std::uint64_t>(chunk.size()) >
        m_limits.maxBytes) {
        m_oversize = true;
        m_reply->abort();
        return;
    }
    m_body.append(chunk);
}

void HttpFetch::cancel() {
    if (isFinished()) {
        return;
    }
    m_cancelled = true;
    if (m_reply) {
        m_reply->abort(); // finished() follows
    }
}

void HttpFetch::onFinished() {
    if (!m_reply || isFinished()) {
        return;
    }
    QNetworkReply* reply = m_reply;
    if (!m_cancelled && !m_oversize) {
        onReadyRead(); // the rest of the body
    }
    m_reply = nullptr;
    reply->deleteLater();
    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    FetchOutcome outcome;
    outcome.url = reply->url().toString().toStdString();
    if (outcome.url.empty()) {
        outcome.url = m_url;
    }
    outcome.status = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    outcome.networkError = reply->error();
    outcome.errorString = reply->errorString().toStdString();
    outcome.cancelled = m_cancelled;
    outcome.oversize = m_oversize;
    outcome.redirectRefused = m_redirectRefused;
    outcome.totalTimeout = m_totalTimeout;
    outcome.body = std::exchange(m_body, {});
    m_stopPoll.stop();
    m_totalTimer.stop();
    m_outcome = std::move(outcome);
    emit finished();
}

std::optional<Error> fetchError(const FetchOutcome& outcome, std::uint64_t maxBytes,
                                const std::function<Error(std::string detail)>& failed,
                                const std::function<Error(std::string detail)>& notFound) {
    if (outcome.cancelled) {
        return makeError(ErrorCode::Cancelled, "download " + outcome.url);
    }
    if (outcome.totalTimeout) {
        return failed(std::format("{}: not complete within the time limit", outcome.url));
    }
    if (outcome.oversize) {
        return failed(
            std::format("{}: the response is larger than {} bytes", outcome.url, maxBytes));
    }
    if (outcome.status == 404) {
        return notFound(outcome.url + ": HTTP 404");
    }
    if (outcome.redirectRefused) {
        return failed(outcome.url + ": redirect to a non-https URL refused");
    }
    if (outcome.networkError != QNetworkReply::NoError || outcome.status < 200 ||
        outcome.status >= 300) {
        // Our own aborts are handled above, so a cancelled transfer is the transfer timeout.
        const bool timedOut = outcome.networkError == QNetworkReply::OperationCanceledError ||
                              outcome.networkError == QNetworkReply::TimeoutError;
        return failed(std::format("{}: HTTP {}, {}{}", outcome.url, outcome.status,
                                  outcome.errorString, timedOut ? " (transfer timeout)" : ""));
    }
    return std::nullopt;
}

} // namespace hio::firmware::detail

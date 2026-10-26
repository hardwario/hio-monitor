#include "hio/update/update_check.h"

#include "hio/core/error.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"

#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QTimeZone>
#include <QUrl>
#include <QVariant>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace hio::update {

namespace {

bool isRateLimited(const QNetworkReply& reply, int status) {
    if (status != 403 && status != 429) {
        return false;
    }
    return reply.rawHeader("X-RateLimit-Remaining") == "0" || reply.hasRawHeader("Retry-After");
}

std::string rateLimitDetail(const QNetworkReply& reply, int status, const std::string& url) {
    std::string detail = std::format("{}: HTTP {}", url, status);
    bool ok = false;
    const qint64 reset = reply.rawHeader("X-RateLimit-Reset").toLongLong(&ok);
    if (ok) {
        detail += std::format(", the limit resets at {}",
                              QDateTime::fromSecsSinceEpoch(reset, QTimeZone::UTC)
                                  .toString(Qt::ISODate)
                                  .toStdString());
    }
    if (reply.hasRawHeader("Retry-After")) {
        detail += std::format(", retry after {} s", reply.rawHeader("Retry-After").toStdString());
    }
    return detail;
}

} // namespace

UpdateCheck::UpdateCheck(QObject* parent) : QObject(parent) {}

UpdateCheck::~UpdateCheck() {
    if (m_reply) {
        QObject::disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void UpdateCheck::start(QNetworkAccessManager& network, const QUrl& url, const SemVer& current,
                        bool includePrerelease) {
    cancel();
    m_body.clear();
    m_current = current;
    m_includePrerelease = includePrerelease;
    m_cancelled = false;
    m_oversize = false;

    QNetworkRequest request(url);
    request.setTransferTimeout(m_timeout);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString::fromStdString(std::format("hio-monitor/{}", buildInfo().version)));
    qCInfo(log::lcUpdate).noquote() << "Checking for updates:" << url.toString();
    m_reply = network.get(request);
    QObject::connect(m_reply, &QNetworkReply::readyRead, this, &UpdateCheck::onReadyRead);
    QObject::connect(m_reply, &QNetworkReply::finished, this, &UpdateCheck::onFinished);
}

void UpdateCheck::cancel() {
    if (m_reply) {
        m_cancelled = true;
        m_reply->abort(); // finished() follows synchronously
    }
}

void UpdateCheck::onReadyRead() {
    if (!m_reply || m_oversize) {
        return;
    }
    const QByteArray chunk = m_reply->readAll();
    if (static_cast<std::size_t>(m_body.size()) + static_cast<std::size_t>(chunk.size()) >
        maxResponseBytes) {
        m_oversize = true;
        m_body.clear();
        m_reply->abort();
        return;
    }
    m_body.append(chunk);
}

void UpdateCheck::onFinished() {
    if (!m_reply) {
        return;
    }
    if (!m_cancelled && !m_oversize) {
        onReadyRead(); // the rest of the body
    }
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    const std::string url = reply->url().toString().toStdString();
    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const QNetworkReply::NetworkError networkError = reply->error();

    if (m_cancelled) {
        finish(std::unexpected(makeError(ErrorCode::Cancelled, "update check " + url)));
        return;
    }
    if (m_oversize) {
        finish(std::unexpected(makeError(
            ErrorCode::UpdateTooLarge,
            std::format("{}: the response is larger than {} bytes", url, maxResponseBytes))));
        return;
    }
    if (isRateLimited(*reply, status)) {
        finish(std::unexpected(
            makeError(ErrorCode::UpdateRateLimited, rateLimitDetail(*reply, status, url))));
        return;
    }
    if (status == 0 || networkError == QNetworkReply::InsecureRedirectError ||
        networkError == QNetworkReply::TooManyRedirectsError ||
        networkError == QNetworkReply::OperationCanceledError ||
        networkError == QNetworkReply::TimeoutError) {
        // No HTTP answer: no connection, DNS, TLS, the transfer timeout (Qt aborts the reply), or
        // a redirect to another origin.
        const bool timedOut = networkError == QNetworkReply::OperationCanceledError ||
                              networkError == QNetworkReply::TimeoutError;
        finish(std::unexpected(
            makeError(ErrorCode::UpdateNetwork,
                      std::format("{}: {}{}", url, reply->errorString().toStdString(),
                                  timedOut ? " (transfer timeout)" : ""))));
        return;
    }
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
        finish(std::unexpected(makeError(
            ErrorCode::UpdateBadResponse,
            std::format("{}: HTTP {}, {}", url, status, reply->errorString().toStdString()))));
        return;
    }
    auto latest = pickLatest(m_body, m_includePrerelease);
    m_body.clear();
    if (!latest) {
        Error error = std::move(latest).error();
        error.detail = std::format("{}: {}", url, error.detail);
        finish(std::unexpected(std::move(error)));
        return;
    }
    CheckResult result = makeCheckResult(m_current, std::move(*latest));
    qCInfo(log::lcUpdate).noquote()
        << "Update check:"
        << QString::fromStdString(result.latest ? result.latest->version.toString() : "no release")
        << (result.newer ? "is newer" : "is not newer") << "than"
        << QString::fromStdString(result.current.toString());
    finish(std::move(result));
}

void UpdateCheck::finish(Result<CheckResult> result) {
    if (!result) {
        qCInfo(log::lcUpdate).noquote()
            << "Update check failed:"
            << QString::fromStdString(std::string(idOf(result.error().code)))
            << QString::fromStdString(result.error().detail);
    }
    m_result = std::move(result);
    emit finished(*m_result);
}

Result<CheckResult> waitForCheck(UpdateCheck& check) {
    if (check.isRunning()) {
        QEventLoop loop;
        QObject::connect(&check, &UpdateCheck::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (const auto& result = check.result()) {
        return *result;
    }
    return std::unexpected(makeError(ErrorCode::InternalUnexpected, "update check did not run"));
}

} // namespace hio::update

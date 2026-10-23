#include "hio/mcp/tool_call.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/json_rpc.h"

#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QLoggingCategory>
#include <QPointer>
#include <QString>
#include <QStringLiteral>
#include <QtCore/qtmetamacros.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace hio::mcp {

namespace {

// A call created already finished (ToolCall::completed).
class CompletedCall final : public ToolCall {
public:
    explicit CompletedCall(hio::Result<ToolResult> result) { finish(std::move(result)); }
};

} // namespace

ToolResult toolResult(const QJsonObject& structured) {
    return ToolResult{.structured = structured,
                      .text = QString::fromUtf8(serialize(structured)),
                      .isError = false};
}

ToolResult toolErrorResult(const hio::Error& error) {
    QJsonObject structured;
    structured.insert(QStringLiteral("error"), hio::toJson(error));
    QString text =
        QLatin1StringView(idOf(error.code)) + QStringLiteral(": ") + toQString(error.message);
    if (!error.suggestion.empty()) {
        text += QStringLiteral(" — ") + toQString(error.suggestion);
    }
    return ToolResult{.structured = structured, .text = text, .isError = true};
}

QJsonObject toolResultJson(const ToolResult& result) {
    QJsonObject text;
    text.insert(QStringLiteral("type"), QStringLiteral("text"));
    text.insert(QStringLiteral("text"), result.text);
    QJsonObject json;
    json.insert(QStringLiteral("content"), QJsonArray{text});
    json.insert(QStringLiteral("structuredContent"), result.structured);
    json.insert(QStringLiteral("isError"), result.isError);
    return json;
}

ToolCall::~ToolCall() {
    if (m_consentPending && m_consentCancel) {
        m_consentCancel();
    }
}

std::unique_ptr<ToolCall> ToolCall::completed(hio::Result<ToolResult> result) {
    return std::make_unique<CompletedCall>(std::move(result));
}

void ToolCall::cancel() {
    if (m_cancelled || isFinished()) {
        return;
    }
    m_cancelled = true;
    if (m_consentPending) {
        m_consentPending = false;
        auto withdraw = std::exchange(m_consentCancel, {});
        if (withdraw) {
            withdraw();
        }
    }
    onCancel();
}

void ToolCall::onCancel() { finish(std::unexpected(makeError(ErrorCode::Cancelled))); }

void ToolCall::finish(hio::Result<ToolResult> result) {
    if (isFinished()) {
        qCWarning(hio::log::lcMcp) << "ToolCall::finish called twice; ignored";
        return;
    }
    if (m_consentPending) {
        m_consentPending = false;
        auto withdraw = std::exchange(m_consentCancel, {});
        if (withdraw) {
            withdraw();
        }
    }
    m_result = std::move(result);
    emit finished(*m_result);
}

void ToolCall::reportProgress(double done, std::optional<double> total, const QString& message) {
    if (isFinished() || m_cancelled) {
        return;
    }
    emit progress(done, total, message);
}

void ToolCall::requestConsent(IConsentBroker& broker, ConsentRequest request,
                              std::function<void(ConsentDecision)> done) {
    if (isFinished() || m_cancelled) {
        return;
    }
    if (m_consentPending) {
        done(std::unexpected(
            makeError(ErrorCode::InternalUnexpected, "a consent request is already pending")));
        return;
    }
    m_consentPending = true;
    const std::uint64_t token = ++m_consentToken;
    const ConsentCategories categories = request.categories;
    const QPointer<ToolCall> self(this);
    auto withdraw =
        broker.request(std::move(request), [self, token, categories,
                                            done = std::move(done)](ConsentDecision decision) {
            if (self.isNull() || !self->m_consentPending || self->m_consentToken != token) {
                return;
            }
            self->m_consentPending = false;
            self->m_consentCancel = {};
            if (self->m_cancelled || self->isFinished()) {
                return;
            }
            if (decision) {
                self->m_granted |= categories;
            }
            done(std::move(decision));
        });
    if (m_consentPending && m_consentToken == token) {
        m_consentCancel = std::move(withdraw);
    }
}

} // namespace hio::mcp

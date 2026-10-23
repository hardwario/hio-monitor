#pragma once

// Consent seam of the MCP server (ADR-0101, plan/features/0013 "Device and consent seams").
//
// hio_mcp never decides consent itself: tool handlers ask an IConsentBroker through
// ToolCall::requestConsent() after validating their arguments, so the request carries the device
// and the details (file name, size, address range). The CLI's FlagConsentBroker answers at once
// from --allow; the GUI broker (plan 0014) shows a prompt. As a safety net the server refuses the
// success result of a call whose ToolSpec has static categories (other than Connect) that were not
// granted during the call (internal.unexpected, logged).
//
// Thread safety: owner thread only.

#include "hio/core/result.h"
#include "hio/mcp/request_context.h"

#include <QFlags>
#include <QJsonObject>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hio::mcp {

enum class ConsentCategory : std::uint8_t {
    Flash = 1,       // flash, flash_lte, write_flash outside UICR
    Erase = 2,       // erase, recover
    MemoryWrite = 4, // write_memory
    Uicr = 8,        // any write touching UICR (requested at run time)
    Connect = 16,    // opening a new device (GUI only; the CLI always allows it)
};
Q_DECLARE_FLAGS(ConsentCategories, ConsentCategory)

// Stable ids: "flash", "erase", "memory-write", "uicr", "connect" (the CLI --allow names).
[[nodiscard]] std::string_view consentCategoryId(ConsentCategory category) noexcept;
[[nodiscard]] std::optional<ConsentCategory> consentCategoryFromId(std::string_view id) noexcept;
// The ids of the set categories in the enum order.
[[nodiscard]] std::vector<std::string_view> consentCategoryIds(ConsentCategories categories);

struct ConsentRequest {
    ClientInfo client;
    std::string clientKey; // RequestContext::clientKey ("remember for this client")
    std::string tool;
    std::string deviceId;
    ConsentCategories categories;
    // For the prompt, e.g. {"file": "...", "size": 123, "target": "app", "range": "0x...-0x..."}
    QJsonObject details;
};

// Success = allowed. Otherwise the error the tool returns: mcp.tool_not_allowed (CLI, call-site
// text naming the --allow flag), mcp.consent_denied, mcp.consent_timeout (GUI).
using ConsentDecision = hio::Status;

class IConsentBroker {
public:
    IConsentBroker() = default;
    IConsentBroker(const IConsentBroker&) = delete;
    IConsentBroker& operator=(const IConsentBroker&) = delete;
    IConsentBroker(IConsentBroker&&) = delete;
    IConsentBroker& operator=(IConsentBroker&&) = delete;
    virtual ~IConsentBroker() = default;

    // Calls `done` exactly once (possibly before returning), unless the returned cancel functor
    // was called first: then `done` is never called. The functor withdraws a pending prompt; it
    // may be empty when there is nothing to withdraw.
    [[nodiscard]] virtual std::function<void()>
    request(ConsentRequest request, std::function<void(ConsentDecision)> done) = 0;
    // The client's legacy session ended, or a modern client was idle for
    // Limits::modernClientIdle: forget "allow for this client" answers.
    virtual void clientGone(std::string_view clientKey) = 0;
};

// A ConsentRequest for the call described by `context` (client, key and tool).
[[nodiscard]] ConsentRequest makeConsentRequest(const RequestContext& context,
                                                ConsentCategories categories, std::string deviceId,
                                                QJsonObject details = {});

} // namespace hio::mcp

Q_DECLARE_OPERATORS_FOR_FLAGS(hio::mcp::ConsentCategories)

#pragma once

// FlagConsentBroker - consent of `hio mcp` from its --allow flags (ADR-0101, plan/features/0013
// "Device and consent seams"). It answers every request at once: allowed when each requested
// category was given with --allow (`connect` is always allowed in the CLI), otherwise
// mcp.tool_not_allowed naming the tool and the missing --allow items.
//
// Thread safety: owner thread only (as every IConsentBroker).

#include "hio/core/result.h"
#include "hio/mcp/consent.h"

#include <functional>
#include <string_view>

namespace hio::cli {

// The destructive categories `--allow all` stands for.
[[nodiscard]] hio::mcp::ConsentCategories allDestructiveCategories() noexcept;

// --allow LIST: comma-separated flash, erase, memory-write, uicr or all (spaces around items are
// ignored, empty items too). Errors: usage.invalid_argument for any other item.
[[nodiscard]] Result<hio::mcp::ConsentCategories> parseAllowList(std::string_view list);

class FlagConsentBroker final : public hio::mcp::IConsentBroker {
public:
    explicit FlagConsentBroker(hio::mcp::ConsentCategories allowed) : m_allowed(allowed) {}

    // Calls `done` before returning; the returned functor is empty (nothing to withdraw).
    [[nodiscard]] std::function<void()>
    request(hio::mcp::ConsentRequest request,
            std::function<void(hio::mcp::ConsentDecision)> done) override;
    void clientGone(std::string_view /*clientKey*/) override {} // nothing is remembered

    [[nodiscard]] hio::mcp::ConsentCategories allowed() const noexcept { return m_allowed; }

private:
    hio::mcp::ConsentCategories m_allowed;
};

} // namespace hio::cli

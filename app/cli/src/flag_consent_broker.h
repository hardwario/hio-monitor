#pragma once

// FlagConsentBroker - consent of `hwio mcp` from its --allow flags (ADR-0101, plan/features/0013
// "Device and consent seams"). It answers every request at once: allowed when each requested
// category was given with --allow (`connect` is always allowed in the CLI), otherwise
// mcp.tool_not_allowed naming the tool and the missing --allow items.
//
// Thread safety: owner thread only (as every IConsentBroker).

#include "hwio/core/result.h"
#include "hwio/mcp/consent.h"

#include <functional>
#include <string_view>

namespace hwio::cli {

// The destructive categories `--allow all` stands for.
[[nodiscard]] hwio::mcp::ConsentCategories allDestructiveCategories() noexcept;

// --allow LIST: comma-separated flash, erase, memory-write, uicr or all (spaces around items are
// ignored, empty items too). Errors: usage.invalid_argument for any other item.
[[nodiscard]] Result<hwio::mcp::ConsentCategories> parseAllowList(std::string_view list);

class FlagConsentBroker final : public hwio::mcp::IConsentBroker {
public:
    explicit FlagConsentBroker(hwio::mcp::ConsentCategories allowed) : m_allowed(allowed) {}

    // Calls `done` before returning; the returned functor is empty (nothing to withdraw).
    [[nodiscard]] std::function<void()>
    request(hwio::mcp::ConsentRequest request,
            std::function<void(hwio::mcp::ConsentDecision)> done) override;
    void clientGone(std::string_view /*clientKey*/) override {} // nothing is remembered

    [[nodiscard]] hwio::mcp::ConsentCategories allowed() const noexcept { return m_allowed; }

private:
    hwio::mcp::ConsentCategories m_allowed;
};

} // namespace hwio::cli

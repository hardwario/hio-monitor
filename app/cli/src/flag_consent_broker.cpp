#include "flag_consent_broker.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/mcp/consent.h"

#include <QLoggingCategory>
#include <QString>

#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hio::cli {

namespace {

using hio::mcp::ConsentCategories;
using hio::mcp::ConsentCategory;

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

ConsentCategories allDestructiveCategories() noexcept {
    return ConsentCategories(ConsentCategory::Flash) | ConsentCategory::Erase |
           ConsentCategory::MemoryWrite | ConsentCategory::Uicr;
}

Result<ConsentCategories> parseAllowList(std::string_view list) {
    ConsentCategories categories;
    std::string_view rest = list;
    while (true) {
        const auto comma = rest.find(',');
        const std::string_view item = trimmed(rest.substr(0, comma));
        if (item == "all") {
            categories |= allDestructiveCategories();
        } else if (!item.empty()) {
            const std::optional<ConsentCategory> category = hio::mcp::consentCategoryFromId(item);
            if (!category || *category == ConsentCategory::Connect) {
                Error error = makeError(ErrorCode::UsageInvalidArgument, std::string(item));
                error.message =
                    std::format("Unknown --allow item \"{}\". Use flash, erase, memory-write, "
                                "uicr or all, separated by commas.",
                                item);
                error.cause.clear();
                return std::unexpected(std::move(error));
            }
            categories |= *category;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(comma + 1);
    }
    return categories;
}

std::function<void()>
FlagConsentBroker::request(hio::mcp::ConsentRequest request,
                           std::function<void(hio::mcp::ConsentDecision)> done) {
    ConsentCategories missing = request.categories & ~m_allowed;
    missing &= ~ConsentCategories(ConsentCategory::Connect); // always allowed in the CLI
    if (missing == ConsentCategories()) {
        done(hio::mcp::ConsentDecision{});
        return {};
    }
    std::string items;
    for (const std::string_view id : hio::mcp::consentCategoryIds(missing)) {
        items += items.empty() ? std::string(id) : "," + std::string(id);
    }
    qCInfo(hio::log::lcMcp).noquote() << "MCP consent: tool" << toQString(request.tool)
                                      << "refused; not allowed:" << toQString(items);
    // MCP error texts are plain English (ADR-0103, review R12); std::format, no %n expansion.
    Error error = makeError(ErrorCode::McpToolNotAllowed);
    error.message = std::format("The tool {} is not allowed by this server.", request.tool);
    error.suggestion = std::format("Start the server with --allow {} (or --allow all).", items);
    done(std::unexpected(std::move(error)));
    return {};
}

} // namespace hio::cli

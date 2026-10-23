#include "hio/mcp/consent.h"

#include "hio/mcp/request_context.h"

#include <QJsonObject>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp {

namespace {

constexpr std::array allCategories{ConsentCategory::Flash, ConsentCategory::Erase,
                                   ConsentCategory::MemoryWrite, ConsentCategory::Uicr,
                                   ConsentCategory::Connect};

} // namespace

std::string_view consentCategoryId(ConsentCategory category) noexcept {
    switch (category) {
    case ConsentCategory::Flash:
        return "flash";
    case ConsentCategory::Erase:
        return "erase";
    case ConsentCategory::MemoryWrite:
        return "memory-write";
    case ConsentCategory::Uicr:
        return "uicr";
    case ConsentCategory::Connect:
        return "connect";
    }
    return "flash";
}

std::optional<ConsentCategory> consentCategoryFromId(std::string_view id) noexcept {
    for (const ConsentCategory category : allCategories) {
        if (consentCategoryId(category) == id) {
            return category;
        }
    }
    return std::nullopt;
}

std::vector<std::string_view> consentCategoryIds(ConsentCategories categories) {
    std::vector<std::string_view> ids;
    for (const ConsentCategory category : allCategories) {
        if (categories.testFlag(category)) {
            ids.push_back(consentCategoryId(category));
        }
    }
    return ids;
}

ConsentRequest makeConsentRequest(const RequestContext& context, ConsentCategories categories,
                                  std::string deviceId, QJsonObject details) {
    return ConsentRequest{.client = context.client,
                          .clientKey = context.clientKey,
                          .tool = context.tool,
                          .deviceId = std::move(deviceId),
                          .categories = categories,
                          .details = std::move(details)};
}

} // namespace hio::mcp

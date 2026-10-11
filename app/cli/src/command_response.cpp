#include "command_response.h"

#include <string>
#include <utility>
#include <vector>

namespace hio::cli {

bool CommandResponse::add(std::vector<std::string>& target, std::string text) {
    // Once truncated nothing more is collected, so the kept lines are the first ones in order.
    if (m_truncated || lines() >= maxLines || m_bytes + text.size() > maxBytes) {
        m_truncated = true;
        return false;
    }
    m_bytes += text.size();
    target.push_back(std::move(text));
    return true;
}

} // namespace hio::cli

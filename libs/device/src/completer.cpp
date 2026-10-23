#include "hio/device/completer.h"

#include "hio/device/command_tree.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hio::device {

namespace {

struct SplitInput {
    std::vector<std::string> path; // the complete words before the current one
    std::string_view prefix;       // the input up to the current word
    std::string_view current;      // the word being typed ("" after a space)
};

SplitInput split(std::string_view input) {
    SplitInput result;
    const std::size_t lastSpace = input.find_last_of(' ');
    const std::size_t start = lastSpace == std::string_view::npos ? 0 : lastSpace + 1;
    result.prefix = input.substr(0, start);
    result.current = input.substr(start);
    std::string_view rest = result.prefix;
    while (!rest.empty()) {
        const std::size_t begin = rest.find_first_not_of(' ');
        if (begin == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(begin);
        const std::size_t end = std::min(rest.find(' '), rest.size());
        result.path.emplace_back(rest.substr(0, end));
        rest.remove_prefix(end);
    }
    return result;
}

// The children of the path's node that start with the current word (known children only).
std::vector<const CommandNode*> candidates(const CommandTree* tree, const SplitInput& input) {
    std::vector<const CommandNode*> found;
    if (tree == nullptr) {
        return found;
    }
    const CommandNode* node = tree->find(input.path);
    if (node == nullptr || !node->childrenKnown) {
        return found;
    }
    for (const CommandNode& child : node->children) {
        if (child.name.starts_with(input.current)) {
            found.push_back(&child);
        }
    }
    return found;
}

} // namespace

std::vector<Suggestion> Completer::suggest(std::string_view input,
                                           const std::deque<std::string>& history,
                                           std::size_t max) const {
    std::vector<Suggestion> rows;
    if (input.find_first_not_of(' ') == std::string_view::npos || max == 0) {
        return rows;
    }
    const SplitInput parts = split(input);
    for (const CommandNode* node : candidates(m_tree, parts)) {
        if (rows.size() >= max) {
            return rows;
        }
        rows.push_back(Suggestion{.kind = Suggestion::Kind::Command,
                                  .text = std::string(parts.prefix) + node->name + " ",
                                  .label = node->name,
                                  .help = node->help});
    }
    std::set<std::string_view> seen;
    for (auto it = history.rbegin(); it != history.rend() && rows.size() < max; ++it) {
        const std::string& entry = *it;
        if (entry.size() <= input.size() || !entry.starts_with(input) ||
            !seen.insert(entry).second) {
            continue;
        }
        rows.push_back(Suggestion{
            .kind = Suggestion::Kind::History, .text = entry, .label = entry, .help = {}});
    }
    return rows;
}

Completion Completer::complete(std::string_view input) const {
    const SplitInput parts = split(input);
    const std::vector<const CommandNode*> found = candidates(m_tree, parts);
    Completion completion{.replacement = std::string(input), .candidates = {}};
    completion.candidates.reserve(found.size());
    for (const CommandNode* node : found) {
        completion.candidates.push_back(node->name);
    }
    if (found.size() == 1) {
        completion.replacement = std::string(parts.prefix) + found.front()->name + " ";
    } else if (found.size() > 1) {
        std::string_view common = found.front()->name;
        for (const CommandNode* node : found) {
            const auto mismatch = std::ranges::mismatch(common, node->name);
            common = common.substr(
                0, static_cast<std::size_t>(std::distance(common.begin(), mismatch.in1)));
        }
        if (common.size() > parts.current.size()) {
            completion.replacement = std::string(parts.prefix) + std::string(common);
        }
    }
    return completion;
}

std::optional<std::string> Completer::historyHint(std::string_view input,
                                                  const std::deque<std::string>& history) {
    if (input.empty()) {
        return std::nullopt;
    }
    for (const std::string& entry : std::views::reverse(history)) {
        if (entry.size() > input.size() && entry.starts_with(input)) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> Completer::pathToLearn(std::string_view input) const {
    if (m_tree == nullptr || m_tree->empty()) {
        return std::nullopt;
    }
    const SplitInput parts = split(input);
    if (parts.path.empty()) {
        return std::nullopt;
    }
    const CommandNode* node = m_tree->find(parts.path);
    if (node == nullptr || node->childrenKnown || parts.path.size() >= CommandTree::maxDepth) {
        return std::nullopt;
    }
    return parts.path;
}

} // namespace hio::device

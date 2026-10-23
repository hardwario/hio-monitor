#include "hio/device/command_tree.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/line_assembler.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::device {

namespace {

constexpr int schemaVersion = 1;

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.';
}

std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// At most `max` bytes, not ending inside a UTF-8 character.
std::string cutUtf8(std::string_view text, std::size_t max) {
    if (text.size() <= max) {
        return std::string(text);
    }
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return std::string(text.substr(0, cut));
}

std::size_t countNodes(const CommandNode& node) {
    std::size_t count = node.children.size();
    for (const CommandNode& child : node.children) {
        count += countNodes(child);
    }
    return count;
}

QJsonArray toJsonArray(const std::vector<CommandNode>& nodes) {
    QJsonArray array;
    for (const CommandNode& node : nodes) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), toQString(node.name));
        object.insert(QStringLiteral("help"), toQString(node.help));
        if (node.childrenKnown) {
            object.insert(QStringLiteral("children"), toJsonArray(node.children));
        }
        array.append(object);
    }
    return array;
}

// Reads `array` into `out` within the bounds (`budget` = nodes still allowed). False on a
// malformed entry.
bool fromJsonArray(const QJsonArray& array, std::vector<CommandNode>& out, std::size_t depth,
                   std::size_t& budget) {
    for (const auto& value : array) {
        if (!value.isObject()) {
            return false;
        }
        const QJsonObject object = value.toObject();
        const std::string name = toStdString(object.value(QStringLiteral("name")).toString());
        if (!isCommandName(name)) {
            return false;
        }
        if (budget == 0 || std::ranges::find(out, name, &CommandNode::name) != out.end()) {
            continue;
        }
        --budget;
        CommandNode node{.name = name,
                         .help =
                             cutUtf8(toStdString(object.value(QStringLiteral("help")).toString()),
                                     CommandTree::maxHelpBytes),
                         .children = {},
                         .childrenKnown = false};
        const QJsonValue children = object.value(QStringLiteral("children"));
        if (children.isArray() && depth < CommandTree::maxDepth) {
            node.childrenKnown = true;
            if (!fromJsonArray(children.toArray(), node.children, depth + 1, budget)) {
                return false;
            }
        }
        out.push_back(std::move(node));
    }
    return true;
}

} // namespace

bool isCommandName(std::string_view name) noexcept {
    return !name.empty() && name.size() <= CommandTree::maxNameBytes &&
           std::ranges::all_of(name, isNameChar);
}

const CommandNode* CommandTree::find(std::span<const std::string> path) const noexcept {
    const CommandNode* node = &m_root;
    for (const std::string& word : path) {
        const auto it = std::ranges::find(node->children, word, &CommandNode::name);
        if (it == node->children.end()) {
            return nullptr;
        }
        node = &*it;
    }
    return node;
}

bool CommandTree::setChildren(std::span<const std::string> path,
                              std::span<const CommandEntry> entries) {
    if (path.size() >= maxDepth) {
        return false;
    }
    CommandNode* node = &m_root;
    for (const std::string& word : path) {
        const auto it = std::ranges::find(node->children, word, &CommandNode::name);
        if (it == node->children.end()) {
            return false;
        }
        node = &*it;
    }
    const std::size_t removed = countNodes(*node);
    m_nodes -= std::min(m_nodes, removed);
    node->children.clear();
    for (const CommandEntry& entry : entries) {
        if (m_nodes >= maxNodes) {
            break;
        }
        if (!isCommandName(entry.name) ||
            std::ranges::find(node->children, entry.name, &CommandNode::name) !=
                node->children.end()) {
            continue;
        }
        node->children.push_back(CommandNode{.name = entry.name,
                                             .help = cutUtf8(entry.help, maxHelpBytes),
                                             .children = {},
                                             .childrenKnown = false});
        ++m_nodes;
    }
    node->childrenKnown = true;
    return true;
}

QJsonObject CommandTree::toJson() const {
    QJsonObject json;
    json.insert(QStringLiteral("schema"), schemaVersion);
    json.insert(QStringLiteral("root"), toJsonArray(m_root.children));
    return json;
}

Result<CommandTree> CommandTree::fromJson(const QJsonObject& json) {
    if (json.value(QStringLiteral("schema")).toInt() != schemaVersion ||
        !json.value(QStringLiteral("root")).isArray()) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, "command tree: schema"));
    }
    CommandTree tree;
    std::size_t budget = maxNodes;
    if (!fromJsonArray(json.value(QStringLiteral("root")).toArray(), tree.m_root.children, 1,
                       budget)) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "command tree: malformed entry"));
    }
    tree.m_root.childrenKnown = true;
    tree.m_nodes = maxNodes - budget;
    return tree;
}

std::vector<CommandEntry> parseHelp(std::span<const std::string> lines) {
    std::vector<CommandEntry> entries;
    bool inList = false;
    std::size_t entryIndent = 0; // the indentation of the entry lines
    for (const std::string& raw : lines) {
        const std::string cleaned = stripAnsiCsi(raw);
        std::string_view line = cleaned;
        while (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const std::string_view trimmed = trim(line);
        if (trimmed == "Available commands:" || trimmed == "Subcommands:") {
            inList = true;
            continue;
        }
        if (!inList) {
            continue;
        }
        if (trimmed.empty() || !(line.starts_with(' ') || line.starts_with('\t'))) {
            inList = false; // the end of the list (empty line, prompt, other text)
            continue;
        }
        const std::size_t indent = line.find_first_not_of(" \t");
        // "<name> :<help>" or "<name>: <help>" (the colon after optional spaces).
        const std::size_t nameEnd = std::min(trimmed.find_first_of(" \t:"), trimmed.size());
        const std::string_view name = trimmed.substr(0, nameEnd);
        const std::string_view rest = trim(trimmed.substr(nameEnd));
        if (isCommandName(name) && rest.starts_with(':')) {
            if (entries.size() >= CommandTree::maxNodes) {
                break;
            }
            entries.push_back(
                CommandEntry{.name = std::string(name),
                             .help = cutUtf8(trim(rest.substr(1)), CommandTree::maxHelpBytes)});
            entryIndent = indent;
            continue;
        }
        // A wrapped help text is indented deeper than the entries; other lines are skipped.
        if (!entries.empty() && indent > entryIndent &&
            entries.back().help.size() < CommandTree::maxHelpBytes) {
            std::string& help = entries.back().help;
            if (!help.empty()) {
                help += ' ';
            }
            help += trimmed;
            help = cutUtf8(help, CommandTree::maxHelpBytes);
        }
    }
    return entries;
}

} // namespace hio::device

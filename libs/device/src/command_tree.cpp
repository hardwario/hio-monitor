#include "hwio/device/command_tree.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/line_assembler.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::device {

namespace {

constexpr int schemaVersion = 2; // 2: the dashH flag (ADR-0178)

// The line Zephyr's `help` prints only with CONFIG_SHELL_HELP (ADR-0178, decision D4).
constexpr std::string_view dashHMarker = "<-h> or <--help>";

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

std::string_view withoutCr(std::string_view line) {
    while (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return line;
}

// Continues a wrapped help text (within maxHelpBytes).
void appendHelp(std::string& help, std::string_view text) {
    if (help.size() >= CommandTree::maxHelpBytes) {
        return;
    }
    if (!help.empty()) {
        help += ' ';
    }
    help += text;
    help = cutUtf8(help, CommandTree::maxHelpBytes);
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
    json.insert(QStringLiteral("dashH"), m_dashHSupported);
    json.insert(QStringLiteral("root"), toJsonArray(m_root.children));
    return json;
}

Result<CommandTree> CommandTree::fromJson(const QJsonObject& json) {
    if (json.value(QStringLiteral("schema")).toInt() != schemaVersion ||
        !json.value(QStringLiteral("dashH")).isBool() ||
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
    tree.m_dashHSupported = json.value(QStringLiteral("dashH")).toBool();
    return tree;
}

std::vector<CommandEntry> parseHelp(std::span<const std::string> lines) {
    std::vector<CommandEntry> entries;
    bool inList = false;
    bool bareNames = false;      // "Available commands:" without CONFIG_SHELL_HELP (D3)
    std::size_t entryIndent = 0; // the indentation of the entry lines
    for (const std::string& raw : lines) {
        const std::string cleaned = stripAnsiCsi(raw);
        const std::string_view line = withoutCr(cleaned);
        const std::string_view trimmed = trim(line);
        if (trimmed == "Available commands:" || trimmed == "Subcommands:") {
            inList = true;
            bareNames = trimmed == "Available commands:";
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
        const bool withHelp = isCommandName(name) && rest.starts_with(':');
        // A wrapped help text is indented deeper than the entries.
        if (!withHelp && !entries.empty() && indent > entryIndent) {
            appendHelp(entries.back().help, trimmed);
            continue;
        }
        // A bare "<name>" (no help text) at the entries' indentation (D3); other lines are skipped.
        if (!withHelp && !(bareNames && isCommandName(name) && rest.empty())) {
            continue;
        }
        if (entries.size() >= CommandTree::maxNodes) {
            break;
        }
        entries.push_back(
            CommandEntry{.name = std::string(name),
                         .help = withHelp ? cutUtf8(trim(rest.substr(1)), CommandTree::maxHelpBytes)
                                          : std::string()});
        entryIndent = indent;
    }
    return entries;
}

bool helpSupportsDashH(std::span<const std::string> lines) {
    return std::ranges::any_of(lines, [](const std::string& raw) {
        const std::string cleaned = stripAnsiCsi(raw);
        return withoutCr(cleaned).contains(dashHMarker);
    });
}

} // namespace hwio::device

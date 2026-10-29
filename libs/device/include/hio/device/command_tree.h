#pragma once

// CommandTree - the device shell's commands as learned from Zephyr `help` (top level) and
// `<command> -h` (subcommands) (plan/features/0021, ADR-0170). Used by Completer (GUI and CLI) and
// stored per firmware by CommandTreeCache.
//
// Bounds (ADR-0170): at most maxNodes nodes (the root not counted), names of at most maxNameBytes
// bytes, help texts cut to maxHelpBytes, at most maxDepth levels. Entries beyond a bound are
// dropped. The JSON form is versioned ("schema": 2) and checked on read.
//
// The tree also records whether the shell handles `<command> -h` itself (plan 0022, ADR-0178):
// only then are subcommands queried with `-h`.
//
// parseHelp() is pure: it reads the entries listed after "Available commands:" (help) or
// "Subcommands:" (-h): indented lines "<name> :<help>" / "<name>: <help>"; in the
// "Available commands:" list also a bare "<name>" (Zephyr without CONFIG_SHELL_HELP, empty help);
// an indented line without a name continues the help text of the entry before it; a line that is
// not indented (prompt, other text) or an empty line ends the list. ANSI CSI sequences are removed
// first.
//
// Thread safety: a value type; no shared state.

#include "hio/core/result.h"

#include <QJsonObject>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::device {

struct CommandEntry {
    std::string name;
    std::string help;
};

struct CommandNode {
    std::string name;
    std::string help;
    std::vector<CommandNode> children;
    bool childrenKnown = false; // the subcommands were queried (an empty list = none)
};

class CommandTree {
public:
    static constexpr std::size_t maxNodes = 2000;
    static constexpr std::size_t maxNameBytes = 64;
    static constexpr std::size_t maxHelpBytes = 256;
    static constexpr std::size_t maxDepth = 8;

    CommandTree() = default;

    // The root (no name); its children are the top-level commands.
    [[nodiscard]] const CommandNode& root() const noexcept { return m_root; }
    [[nodiscard]] std::size_t nodeCount() const noexcept { return m_nodes; }
    [[nodiscard]] bool empty() const noexcept { return m_root.children.empty(); }

    // True when the shell's `help` showed that it handles `<command> -h` (ADR-0178).
    [[nodiscard]] bool dashHSupported() const noexcept { return m_dashHSupported; }
    void setDashHSupported(bool supported) noexcept { m_dashHSupported = supported; }

    // The node at `path` (command words; the root for an empty path), nullptr when unknown.
    [[nodiscard]] const CommandNode* find(std::span<const std::string> path) const noexcept;

    // Replaces the children of the node at `path` with `entries` (names checked, duplicates and
    // entries beyond the bounds dropped) and marks them known. False when `path` is unknown or
    // deeper than maxDepth.
    bool setChildren(std::span<const std::string> path, std::span<const CommandEntry> entries);

    // {"schema":2,"dashH":bool,"root":[{"name","help","children":[...]?}]} (children only when
    // known).
    [[nodiscard]] QJsonObject toJson() const;
    // Errors: usage.invalid_argument (not a tree of this schema, also schema 1 without the dashH
    // flag; bounds are applied).
    [[nodiscard]] static Result<CommandTree> fromJson(const QJsonObject& json);

private:
    CommandNode m_root{.name = {}, .help = {}, .children = {}, .childrenKnown = false};
    std::size_t m_nodes = 0;
    bool m_dashHSupported = false;
};

// True for a valid command name: 1..maxNameBytes characters [A-Za-z0-9_.-].
[[nodiscard]] bool isCommandName(std::string_view name) noexcept;

[[nodiscard]] std::vector<CommandEntry> parseHelp(std::span<const std::string> lines);

// True when a line of the `help` answer (ANSI CSI removed, `\r` trimmed) contains the
// CONFIG_SHELL_HELP marker `<-h> or <--help>` (ADR-0178). Pure.
[[nodiscard]] bool helpSupportsDashH(std::span<const std::string> lines);

} // namespace hio::device

#pragma once

// Completer - console suggestions and Tab completion (plan/features/0021, ADR-0170), pure and
// shared by the GUI shell input and the CLI console (replxx hints and completion).
//
// The input is split into words at spaces; the current word is the text after the last space
// (empty after a trailing space), the words before it are the command path.
//   - suggest(): the device commands of the path's node that start with the current word (tree
//     order; each accepted as path + name + " "), then the history entries that start with the
//     whole input (newest first, unique, not equal to the input); at most `max` (50) rows; none
//     for an empty input.
//   - complete(): Tab - the only command candidate (+ " "), or the candidates' longest common
//     prefix; the input unchanged when there is none. History is not used.
//   - historyHint(): the ghost text source - the newest history entry that starts with the input
//     and is longer than it.
//   - pathToLearn(): the command path whose subcommands are not known yet (to be learned with
//     `<command> -h`, plan 0021 phase 2).
// Without a tree (not learned yet, or no device) only history is suggested.

#include "hio/core/settings.h"
#include "hio/device/command_tree.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hio::device {

// The console settings of plan 0021 (ADR-0170), shared by the GUI (ConsoleSettings) and the CLI
// console (`hio chester app console` reads both; --no-suggestions overrides for one run).
inline constexpr SettingKey<bool> suggestionsSetting{.path = "console/suggestions",
                                                     .defaultValue = true};
inline constexpr SettingKey<bool> readCommandsOnConnectSetting{
    .path = "console/readCommandsOnConnect", .defaultValue = true};

struct Suggestion {
    enum class Kind : std::uint8_t { Command, History };
    Kind kind = Kind::Command;
    std::string text;  // the whole input line after accepting the row
    std::string label; // what the row shows: the command name / the history entry
    std::string help;  // the command's help ("" for history)
};

struct Completion {
    std::string replacement;             // the new input line
    std::vector<std::string> candidates; // the matching command names (tree order)
};

class Completer {
public:
    static constexpr std::size_t maxSuggestions = 50;

    // `tree` may be nullptr (history only); it must outlive the completer or be replaced first.
    explicit Completer(const CommandTree* tree = nullptr) noexcept : m_tree(tree) {}
    void setTree(const CommandTree* tree) noexcept { m_tree = tree; }
    [[nodiscard]] const CommandTree* tree() const noexcept { return m_tree; }

    // `history`: oldest first (CommandHistory::entries()).
    [[nodiscard]] std::vector<Suggestion> suggest(std::string_view input,
                                                  const std::deque<std::string>& history,
                                                  std::size_t max = maxSuggestions) const;
    [[nodiscard]] Completion complete(std::string_view input) const;
    [[nodiscard]] static std::optional<std::string>
    historyHint(std::string_view input, const std::deque<std::string>& history);
    // The path (command words) that the input has typed past and whose subcommands are unknown;
    // nullopt when there is nothing to learn (no tree, unknown word, children known).
    [[nodiscard]] std::optional<std::vector<std::string>> pathToLearn(std::string_view input) const;

private:
    const CommandTree* m_tree; // non-owning, may be nullptr
};

} // namespace hio::device

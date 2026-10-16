// Redaction of the diagnostic report (ADR-0075).

#include "hio/diag/report.h"

#include <QDir>
#include <QSysInfo>
#include <QtEnvironmentVariables>
#include <QtSystemDetection>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace hio::diag {

namespace {

constexpr std::size_t minShortHostLength = 3;

bool isWordChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || c == '-' || u >= 0x80; // UTF-8 letters too
}

char fold(char c, bool caseInsensitive) {
    return caseInsensitive ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c;
}

// The position of `needle` in `text` from `from`, comparing case-insensitively (ASCII) when asked.
std::size_t findText(std::string_view text, std::string_view needle, std::size_t from,
                     bool caseInsensitive) {
    if (needle.empty() || text.size() < needle.size()) {
        return std::string_view::npos;
    }
    for (std::size_t i = from; i + needle.size() <= text.size(); ++i) {
        bool equal = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (fold(text[i + j], caseInsensitive) != fold(needle[j], caseInsensitive)) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return i;
        }
    }
    return std::string_view::npos;
}

// A character that continues a file or directory name: a word character or '.'.
bool isNameChar(char c) { return isWordChar(c) || c == '.'; }

// How a match must end (and, for whole words, begin).
enum class Boundary : std::uint8_t {
    Word, // a whole word: no word character before or after (user and host names)
    Path, // the home path: no name character after (a separator, a space, a quote, the end)
};

// Replaces `needle` by `replacement` where its boundary allows it.
std::string replaceAll(std::string_view text, std::string_view needle, std::string_view replacement,
                       Boundary boundary, bool caseInsensitive) {
    std::string out;
    out.reserve(text.size());
    std::size_t position = 0;
    while (true) {
        std::size_t found = findText(text, needle, position, caseInsensitive);
        // Skip matches inside a longer word.
        while (found != std::string_view::npos) {
            const std::size_t end = found + needle.size();
            const bool before =
                boundary == Boundary::Word && found > 0 && isWordChar(text[found - 1]);
            const bool after =
                end < text.size() &&
                (boundary == Boundary::Path ? isNameChar(text[end]) : isWordChar(text[end]));
            if (!before && !after) {
                break;
            }
            found = findText(text, needle, found + 1, caseInsensitive);
        }
        if (found == std::string_view::npos) {
            out.append(text.substr(position));
            return out;
        }
        out.append(text.substr(position, found - position));
        out.append(replacement);
        position = found + needle.size();
    }
}

} // namespace

RedactionContext currentRedaction() {
    RedactionContext context;
    context.home = QDir::homePath().toStdString();
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USERNAME");
    }
    context.user = user.toStdString();
    context.host = QSysInfo::machineHostName().toStdString();
#ifdef Q_OS_WIN
    context.caseInsensitive = true;
#endif
    return context;
}

std::string redact(std::string_view text, const RedactionContext& context) {
    std::string result(text);
    std::string home = context.home;
    while (home.size() > 1 && (home.back() == '/' || home.back() == '\\')) {
        home.pop_back();
    }
    if (home.size() > 1) {
        // Only the home folder itself: "/home/jane/x" and "/home/jane" become "~/x" and "~", but a
        // sibling such as "/home/jane_old" or "/home/jane.bak" is another folder (review V13). Its
        // name is then handled by the user-name rule below: "jane.bak" -> "<user>.bak", while
        // "jane_old" is another name and stays (as "jane_old" anywhere else).
        result = replaceAll(result, home, "~", Boundary::Path, context.caseInsensitive);
        // The same path with the other separator (Windows logs mix C:\Users\x and C:/Users/x).
        std::string other = home;
        std::ranges::replace(other, '/', '\x01');
        std::ranges::replace(other, '\\', '/');
        std::ranges::replace(other, '\x01', '\\');
        if (other != home) {
            result = replaceAll(result, other, "~", Boundary::Path, context.caseInsensitive);
        }
    }
    if (context.user.size() > 1) {
        result =
            replaceAll(result, context.user, "<user>", Boundary::Word, context.caseInsensitive);
    }
    if (context.host.size() > 1) {
        result =
            replaceAll(result, context.host, "<host>", Boundary::Word, context.caseInsensitive);
        // The short host name too ("janes-mbp" of "janes-mbp.local", review V13), when it has at
        // least minShortHostLength characters (shorter ones would hit ordinary words).
        if (const std::size_t dot = context.host.find('.');
            dot != std::string::npos && dot >= minShortHostLength) {
            result = replaceAll(result, std::string_view(context.host).substr(0, dot), "<host>",
                                Boundary::Word, context.caseInsensitive);
        }
    }
    return result;
}

} // namespace hio::diag

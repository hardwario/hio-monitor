#pragma once

// Localization helpers (ADR-0027).
//
// Backend texts are translated where they are created: classes use Q_DECLARE_TR_FUNCTIONS(<Name>),
// free functions use QCoreApplication::translate("hwio::<module>", "...") or QT_TRANSLATE_NOOP for
// tables, so that lupdate finds every string. Values are inserted with %1 + QString::arg, never
// with std::format. Context names: "hwio::core", "hwio::gui", later "hwio::jlink", "hwio::device",
// ...
//
// Adding a language = a new i18n/hwio_<lang>.ts file + one entry in supportedLanguages
// (see cmake/HWioTranslations.cmake).

#include <array>
#include <string>
#include <string_view>

namespace hwio::i18n {

struct LanguageInfo {
    std::string_view code;       // ISO 639-1, e.g. "cs"
    std::string_view nativeName; // name of the language in that language, UTF-8
};

inline constexpr std::array supportedLanguages{
    LanguageInfo{.code = "en", .nativeName = "English"},
    LanguageInfo{.code = "cs", .nativeName = "Čeština"},
};

// UTF-8 wrapper over QCoreApplication::translate (thread-safe). Returns sourceText unchanged when
// no translator is installed (e.g. in the CLI, which is English only).
[[nodiscard]] std::string tr(const char* context, const char* sourceText,
                             const char* disambiguation = nullptr, int n = -1);

} // namespace hwio::i18n

#pragma once

// The user documentation links of the app in one table (ADR-0138's central URL table; plan 0028
// D18, ADR-0203). Plan 0029 (customer documentation on docs.hardwario.com) changes only this
// table: the anchors of the topics and the Czech pages. Until then every topic opens the
// HARDWARIO Monitor page and the language is not used.

#include <QStringView>
#include <QUrl>

#include <cstdint>

namespace hwio {

enum class DocsTopic : std::uint8_t {
    Monitor,      // Help → Documentation
    ConsoleBatch, // Console → Batch files (plan 0028 F4)
};

// `language`: "en", "cs" (the UI language).
[[nodiscard]] QUrl docsUrl(DocsTopic topic, QStringView language);

} // namespace hwio

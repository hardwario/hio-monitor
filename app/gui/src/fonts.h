#pragma once

// Bundled fonts (ADR-0048): Inter (Regular, Medium, SemiBold) for the UI and JetBrains Mono
// (Regular, Bold) for consoles and ids, embedded as resources (:/fonts/...).

#include <QString>

namespace hio::gui {

inline const QString uiFontFamily = QStringLiteral("Inter");
inline const QString monoFontFamily = QStringLiteral("JetBrains Mono");

// Registers the bundled fonts and makes Inter 13 px the application font. A font that cannot be
// loaded (e.g. a Git LFS pointer in a clone without LFS) is logged; the system font is used then.
// Returns the number of fonts loaded. GUI thread, after the QGuiApplication exists.
int loadBundledFonts();

} // namespace hio::gui

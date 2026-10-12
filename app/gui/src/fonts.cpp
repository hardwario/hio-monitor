#include "fonts.h"

#include "gui_log.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>

#include <array>

namespace hio::gui {

int loadBundledFonts() {
    static constexpr std::array files{
        ":/fonts/Inter-Regular.ttf",      ":/fonts/Inter-Medium.ttf",
        ":/fonts/Inter-SemiBold.ttf",     ":/fonts/JetBrainsMono-Regular.ttf",
        ":/fonts/JetBrainsMono-Bold.ttf",
    };
    int loaded = 0;
    QStringList families;
    for (const char* file : files) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(file));
        if (id < 0) {
            qCWarning(lcGui) << "The bundled font" << file
                             << "could not be loaded; the system font is used";
            continue;
        }
        ++loaded;
        families += QFontDatabase::applicationFontFamilies(id);
    }
    if (families.contains(uiFontFamily)) {
        QFont font(uiFontFamily);
        font.setPixelSize(13); // design system: fontUi
        QGuiApplication::setFont(font);
    }
    qCDebug(lcGui) << "Bundled fonts loaded:" << loaded << families;
    return loaded;
}

} // namespace hio::gui

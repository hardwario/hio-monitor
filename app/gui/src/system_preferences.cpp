#include "system_preferences.h"

#include <QtEnvironmentVariables>

#ifdef Q_OS_WIN
#include <windows.h>
#elifdef Q_OS_MACOS
#include <QDir>
#include <QSettings>
#include <QString>
#include <QVariant>
#endif

namespace hio::gui {

namespace {

// The OS setting; false where there is none.
bool osRequestsReducedMotion() {
#ifdef Q_OS_WIN
    BOOL animations = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) != 0 &&
           animations == FALSE;
#elifdef Q_OS_MACOS
    const QSettings universalAccess(
        QDir::homePath() + QStringLiteral("/Library/Preferences/com.apple.universalaccess.plist"),
        QSettings::NativeFormat);
    return universalAccess.value(QStringLiteral("reduceMotion"), false).toBool();
#else
    return false;
#endif
}

} // namespace

bool reducedMotionRequested() {
    return qEnvironmentVariableIntValue("HIO_REDUCED_MOTION") == 1 || osRequestsReducedMotion();
}

} // namespace hio::gui

#include "install_paths.h"

#include "hwio/core/json.h"
#include "hwio/core/version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtEnvironmentVariables>

#include <array>
#include <utility>

namespace hwio::gui {

namespace {

QString packageKind() { return toQString(buildInfo().packageKind); }

QString exe(const QString& name) {
#ifdef Q_OS_WIN
    return name + QStringLiteral(".exe");
#else
    return name;
#endif
}

QString installedLicensesDir() {
    const QDir bin(QCoreApplication::applicationDirPath());
    const QString kind = packageKind();
    if (kind == QStringLiteral("macos-bundle")) {
        return bin.absoluteFilePath(QStringLiteral("../Resources/licenses"));
    }
    if (kind == QStringLiteral("msi")) {
        return bin.absoluteFilePath(QStringLiteral("licenses"));
    }
    return bin.absoluteFilePath(QStringLiteral("../licenses"));
}

#ifdef HWIO_GUI_DEV_SOURCE_DIR
// dev builds that were not installed: the files of the source and build trees.
QString devLicenceFile(const QString& fileName) {
    const QString source = QStringLiteral(HWIO_GUI_DEV_SOURCE_DIR);
    const QString build = QStringLiteral(HWIO_GUI_DEV_BINARY_DIR);
    const std::array<std::pair<const char*, QString>, 10> files{{
        {"LICENSE", source + QStringLiteral("/LICENSE")},
        {"Qt-SOURCE.txt", build + QStringLiteral("/licenses/Qt-SOURCE.txt")},
        {"THIRD-PARTY-NOTICES.txt", build + QStringLiteral("/licenses/THIRD-PARTY-NOTICES.txt")},
        {"Inter-LICENSE.txt", source + QStringLiteral("/app/gui/assets/fonts/Inter-LICENSE.txt")},
        {"JetBrainsMono-OFL.txt",
         source + QStringLiteral("/app/gui/assets/fonts/JetBrainsMono-OFL.txt")},
        {"MaterialSymbols-Apache-2.0.txt",
         source + QStringLiteral("/app/gui/assets/icons/LICENSE-Apache-2.0.txt")},
        {"Qt-LGPL-3.0.txt", source + QStringLiteral("/packaging/common/licenses/Qt-LGPL-3.0.txt")},
        {"CLI11-BSD-3-Clause.txt",
         source + QStringLiteral("/packaging/common/licenses/CLI11-BSD-3-Clause.txt")},
        {"replxx-LICENSE.md",
         source + QStringLiteral("/packaging/common/licenses/replxx-LICENSE.md")},
        {"miniz-MIT.txt", source + QStringLiteral("/packaging/common/licenses/miniz-MIT.txt")},
    }};
    for (const auto& [name, path] : files) {
        if (fileName == QString::fromLatin1(name)) {
            return path;
        }
    }
    return {};
}
#endif

} // namespace

QString shellQuote(const QString& text) {
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QUrl licenceFile(const QString& fileName) {
    QString path = QDir(installedLicensesDir()).absoluteFilePath(fileName);
#ifdef HWIO_GUI_DEV_SOURCE_DIR
    if (!QFileInfo::exists(path)) {
        path = devLicenceFile(fileName);
    }
#endif
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return {};
    }
    return QUrl::fromLocalFile(QDir::cleanPath(path));
}

QString cliCommand() {
    const QString kind = packageKind();
    const QDir bin(QCoreApplication::applicationDirPath());
    if (kind == QStringLiteral("deb")) {
        return QStringLiteral("/usr/bin/hwio");
    }
    if (kind == QStringLiteral("appimage")) {
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        return shellQuote(appImage.isEmpty() ? QStringLiteral("<AppImage>") : appImage) +
               QStringLiteral(" hwio");
    }
    if (kind == QStringLiteral("macos-bundle") || kind == QStringLiteral("msi")) {
        return QDir::toNativeSeparators(bin.absoluteFilePath(exe(QStringLiteral("hwio"))));
    }
    // dev: next to the GUI (installed tree) or in the build tree's app/cli.
    const QString installed = bin.absoluteFilePath(exe(QStringLiteral("hwio")));
    if (QFileInfo::exists(installed)) {
        return QDir::toNativeSeparators(installed);
    }
    return QDir::toNativeSeparators(QDir::cleanPath(
        bin.absoluteFilePath(QStringLiteral("../cli/") + exe(QStringLiteral("hwio")))));
}

QString cliSymlinkCommand() {
    if (packageKind() != QStringLiteral("macos-bundle")) {
        return {};
    }
    return QStringLiteral("sudo ln -s %1 /usr/local/bin/hwio").arg(shellQuote(cliCommand()));
}

bool cliOnPath() {
    const QString kind = packageKind();
    return kind == QStringLiteral("deb") || kind == QStringLiteral("msi");
}

} // namespace hwio::gui

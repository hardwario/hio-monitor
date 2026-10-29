// shellQuote of the suggested CLI commands (plan/features/0025, review L-18).

#include "install_paths.h"

#include <QProcess>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtSystemDetection>

#include <gtest/gtest.h>

#include <array>

namespace {

using hwio::gui::shellQuote;

TEST(ShellQuote, WrapsInSingleQuotesAndEscapesSingleQuotes) {
    EXPECT_EQ(shellQuote(QStringLiteral("/Applications/HW.app")),
              QStringLiteral("'/Applications/HW.app'"));
    EXPECT_EQ(shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
    EXPECT_EQ(shellQuote(QStringLiteral("a \"b\" $HOME `id` c")),
              QStringLiteral("'a \"b\" $HOME `id` c'"));
    EXPECT_EQ(shellQuote(QString()), QStringLiteral("''"));
}

#ifndef Q_OS_WIN
// The quoted word reaches a POSIX shell as exactly the original text (nothing is expanded).
TEST(ShellQuote, PosixShellSeesTheOriginalText) {
    const std::array<QString, 5> samples{
        QStringLiteral("/Volumes/My Disk/it's here/HW.app/Contents/MacOS/hwio"),
        QStringLiteral("a\"b\"c"), QStringLiteral("$(touch /tmp/x) ${HOME} $HOME"),
        QStringLiteral("`id`; rm -rf x && echo y | cat"), QStringLiteral("'''")};
    for (const QString& sample : samples) {
        QProcess shell;
        shell.start(
            QStringLiteral("/bin/sh"),
            QStringList{QStringLiteral("-c"), QStringLiteral("printf %s ") + shellQuote(sample)});
        ASSERT_TRUE(shell.waitForFinished(10000)) << sample.toStdString();
        EXPECT_EQ(QString::fromUtf8(shell.readAllStandardOutput()), sample) << sample.toStdString();
    }
}
#endif

} // namespace

// SingleInstance (ADR-0058): a second instance activates the first; a hung first instance is
// reported within the 2 s timeout. The other instance is this binary in its helper mode
// (single_instance_helper.h).

#include "gui_test_support.h"
#include "single_instance.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QUuid>

#include <gtest/gtest.h>

#include <chrono>

namespace {

using hio::gui::SingleInstance;
using hio::gui::test::waitFor;

class SingleInstanceTest : public hio::gui::test::GuiTest {
protected:
    [[nodiscard]] QString lockFile() const {
        return QString::fromStdU16String((root() / "gui.lock").u16string());
    }
    [[nodiscard]] const QString& serverName() const { return m_serverName; }

    // Starts the helper and waits until it is the primary instance.
    bool startHelper(QProcess& process, bool hung) const {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HIO_TEST_SINGLE_INSTANCE"),
                   lockFile() + QLatin1Char('|') + m_serverName);
        if (hung) {
            env.insert(QStringLiteral("HIO_TEST_SINGLE_INSTANCE_HUNG"), QStringLiteral("1"));
        }
        process.setProcessEnvironment(env);
        process.start(QCoreApplication::applicationFilePath(), {});
        QByteArray out;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!out.contains("primary\n")) {
            if (out.contains("not-primary") || std::chrono::steady_clock::now() > deadline ||
                process.state() == QProcess::NotRunning) {
                return false;
            }
            process.waitForReadyRead(100);
            out += process.readAllStandardOutput();
        }
        return true;
    }

    static void stop(QProcess& process) {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    }

private:
    QString m_serverName =
        QStringLiteral("hio-monitor-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
};

TEST_F(SingleInstanceTest, TheFirstInstanceIsPrimary) {
    {
        SingleInstance first(lockFile(), serverName());
        EXPECT_EQ(first.start(), SingleInstance::Result::Primary);
    }
    // Released by the destructor.
    SingleInstance again(lockFile(), serverName());
    EXPECT_EQ(again.start(), SingleInstance::Result::Primary);
}

TEST_F(SingleInstanceTest, ASecondInstanceActivatesTheFirst) {
    QProcess helper;
    ASSERT_TRUE(startHelper(helper, false));
    SingleInstance second(lockFile(), serverName());
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(second.start(), SingleInstance::Result::Activated);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
    ASSERT_TRUE(helper.waitForFinished(10000));
    EXPECT_TRUE(helper.readAllStandardOutput().contains("activated"));
}

TEST_F(SingleInstanceTest, AHungInstanceIsReported) {
    QProcess helper;
    ASSERT_TRUE(startHelper(helper, true));
    SingleInstance second(lockFile(), serverName());
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(second.start(), SingleInstance::Result::NotResponding);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(1900));
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
    // "Start anyway": the hung instance keeps its server; this one does not take it over.
    second.startWithoutLock();
    stop(helper);
}

TEST_F(SingleInstanceTest, AStaleLockIsTakenOver) {
    QProcess helper;
    ASSERT_TRUE(startHelper(helper, true));
    stop(helper); // killed: the lock file stays, its owner is gone
    SingleInstance next(lockFile(), serverName());
    EXPECT_EQ(next.start(), SingleInstance::Result::Primary);
}

TEST(SingleInstanceNames, DefaultServerNameIsSafe) {
    const QString name = SingleInstance::defaultServerName();
    EXPECT_TRUE(name.startsWith(QStringLiteral("hio-monitor-")));
    for (const QChar character : name) {
        EXPECT_TRUE(character.isLetterOrNumber() || character == QLatin1Char('-') ||
                    character == QLatin1Char('_'));
    }
}

} // namespace

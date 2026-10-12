// ShortcutRegistry: duplicate checks, texts, overlay groups (plan 0004, B5; ADR-0051).

#include "gui_test_support.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/shortcut_registry.h"

#include <QKeySequence>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

namespace {

using hio::ErrorCode;
using hio::gui::ShortcutRegistry;

const QString global = ShortcutRegistry::globalScope;

TEST(ShortcutRegistry, AddAndTexts) {
    ShortcutRegistry registry;
    ASSERT_TRUE(registry.add(QStringLiteral("console.find"), QKeySequence(QStringLiteral("Ctrl+F")),
                             QStringLiteral("console"), "Find"));
    EXPECT_EQ(registry.portableText(QStringLiteral("console.find")), QStringLiteral("Ctrl+F"));
    EXPECT_FALSE(registry.sequenceText(QStringLiteral("console.find")).isEmpty());
    EXPECT_EQ(
        registry.tooltip(QStringLiteral("Find"), QStringLiteral("console.find")),
        QStringLiteral("Find (%1)").arg(registry.sequenceText(QStringLiteral("console.find"))));
    EXPECT_EQ(registry.tooltip(QStringLiteral("Find"), QStringLiteral("unknown")),
              QStringLiteral("Find"));
    EXPECT_TRUE(registry.portableText(QStringLiteral("unknown")).isEmpty());
    EXPECT_EQ(registry.count(), 1);
}

TEST(ShortcutRegistry, Duplicates) {
    ShortcutRegistry registry;
    ASSERT_TRUE(registry.add(QStringLiteral("tab.new"), QKeySequence(QStringLiteral("Ctrl+T")),
                             global, "New device tab"));
    ASSERT_TRUE(registry.add(QStringLiteral("flash.go"),
                             QKeySequence(QStringLiteral("Ctrl+Return")), QStringLiteral("flash"),
                             "Flash"));
    const auto expectInvalid = [](const hio::Status& status) {
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, ErrorCode::UsageInvalidArgument);
    };
    // The same id twice.
    expectInvalid(registry.add(QStringLiteral("tab.new"), QKeySequence(QStringLiteral("Ctrl+N")),
                               global, "x"));
    // The same sequence twice in one scope.
    expectInvalid(registry.add(QStringLiteral("flash.other"),
                               QKeySequence(QStringLiteral("Ctrl+Return")), QStringLiteral("flash"),
                               "x"));
    // A page scope against global, both ways.
    expectInvalid(registry.add(QStringLiteral("console.new"),
                               QKeySequence(QStringLiteral("Ctrl+T")), QStringLiteral("console"),
                               "x"));
    expectInvalid(registry.add(QStringLiteral("global.go"),
                               QKeySequence(QStringLiteral("Ctrl+Return")), global, "x"));
    // Empty parts.
    expectInvalid(registry.add(QString(), QKeySequence(QStringLiteral("Ctrl+1")), global, "x"));
    expectInvalid(registry.add(QStringLiteral("a"), QKeySequence(), global, "x"));
    // Two pages may share a sequence.
    EXPECT_TRUE(registry.add(QStringLiteral("console.go"),
                             QKeySequence(QStringLiteral("Ctrl+Return")), QStringLiteral("console"),
                             "Send"));
    EXPECT_EQ(registry.count(), 3);
}

TEST(ShortcutRegistry, IsBounded) {
    ShortcutRegistry registry;
    for (std::size_t i = 0; i < ShortcutRegistry::maxShortcuts; ++i) {
        ASSERT_TRUE(registry.add(QStringLiteral("s%1").arg(i), QKeySequence(Qt::CTRL | Qt::Key_A),
                                 QStringLiteral("page%1").arg(i), "x"));
    }
    EXPECT_FALSE(registry.add(QStringLiteral("one.more"), QKeySequence(Qt::CTRL | Qt::Key_B),
                              QStringLiteral("other"), "x"));
}

TEST(ShortcutRegistry, GroupsMergeAlternatives) {
    ShortcutRegistry registry;
    ASSERT_TRUE(registry.add(QStringLiteral("help.shortcuts"), QKeySequence(QStringLiteral("F1")),
                             global, "Keyboard shortcuts"));
    ASSERT_TRUE(registry.add(QStringLiteral("help.shortcuts.alt"),
                             QKeySequence(QStringLiteral("Ctrl+/")), global, "Keyboard shortcuts"));
    ASSERT_TRUE(registry.add(QStringLiteral("tab.new"), QKeySequence(QStringLiteral("Ctrl+T")),
                             global, "New device tab"));
    ASSERT_TRUE(registry.add(QStringLiteral("console.find"), QKeySequence(QStringLiteral("Ctrl+F")),
                             QStringLiteral("console"), "Find"));
    registry.setScopeTitle(QStringLiteral("console"), "Console");
    const QVariantList groups = registry.groups();
    ASSERT_EQ(groups.size(), 3);
    EXPECT_EQ(groups[0].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Tabs"));
    EXPECT_EQ(groups[1].toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("General"));
    EXPECT_EQ(groups[2].toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("Console"));
    const QVariantList general = groups[1].toMap().value(QStringLiteral("items")).toList();
    ASSERT_EQ(general.size(), 1);
    EXPECT_TRUE(
        general[0].toMap().value(QStringLiteral("keys")).toString().contains(QStringLiteral(", ")));
}

class ShellShortcuts : public hio::gui::test::GuiTest {};

TEST_F(ShellShortcuts, TheShellMapHasNoDuplicates) {
    const std::unique_ptr<hio::gui::AppViewModel> app = makeApp();
    QSet<QString> seen;
    for (const QVariant& entry : app->shortcuts()->entries()) {
        const QVariantMap map = entry.toMap();
        const QString key = map.value(QStringLiteral("scope")).toString() + QLatin1Char(' ') +
                            map.value(QStringLiteral("keys")).toString();
        EXPECT_FALSE(seen.contains(key)) << key.toStdString();
        seen.insert(key);
    }
    // ADR-0051 shell shortcuts, plus Ctrl+1..3 for Console, Flash and the test page.
    const ShortcutRegistry& registry = *app->shortcuts();
    EXPECT_EQ(registry.portableText(QStringLiteral("tab.new")), QStringLiteral("Ctrl+T"));
    EXPECT_EQ(registry.portableText(QStringLiteral("tab.close")), QStringLiteral("Ctrl+W"));
    EXPECT_EQ(registry.portableText(QStringLiteral("tab.next")), QStringLiteral("Ctrl+Tab"));
    EXPECT_EQ(registry.portableText(QStringLiteral("settings.open")), QStringLiteral("Ctrl+,"));
    EXPECT_EQ(registry.portableText(QStringLiteral("help.shortcuts")), QStringLiteral("F1"));
    EXPECT_EQ(registry.portableText(QStringLiteral("device.toggle")),
              QStringLiteral("Ctrl+Shift+D"));
    EXPECT_EQ(registry.portableText(QStringLiteral("device.reconnect")), QStringLiteral("F4"));
    EXPECT_EQ(registry.portableText(QStringLiteral("page.console")), QStringLiteral("Ctrl+1"));
    EXPECT_EQ(registry.portableText(QStringLiteral("page.flash")), QStringLiteral("Ctrl+2"));
    EXPECT_EQ(registry.portableText(QStringLiteral("page.test")), QStringLiteral("Ctrl+3"));
    EXPECT_EQ(registry.portableText(QStringLiteral("test.action")), QStringLiteral("Ctrl+Shift+Y"));
    EXPECT_TRUE(registry.portableText(QStringLiteral("page.settings")).isEmpty());
}

} // namespace

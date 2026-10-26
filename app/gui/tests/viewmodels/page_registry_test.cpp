// PageRegistry: order, lookup, duplicates, translated titles (plan 0004, B1).

#include "viewmodels/page_registry.h"

#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtCore/qttranslation.h>

#include <gtest/gtest.h>

namespace {

using hio::gui::PageDescriptor;
using hio::gui::PageRegistry;
using hio::gui::SettingsSectionDescriptor;

PageDescriptor page(const char* id, int order, bool global = false) {
    return PageDescriptor{.id = QString::fromLatin1(id),
                          .titleSourceText = QT_TRANSLATE_NOOP("Pages", "Console"),
                          .icon = QStringLiteral("console"),
                          .qml = QUrl(QStringLiteral("qrc:/Page.qml")),
                          .order = order,
                          .global = global,
                          .createViewModel = {}};
}

TEST(PageRegistry, PagesAreSortedByOrder) {
    PageRegistry registry;
    registry.add(page("flash", 20));
    registry.add(page("settings", 100, true));
    registry.add(page("console", 10));
    ASSERT_EQ(registry.pages().size(), 3U);
    EXPECT_EQ(registry.pages()[0].id, QStringLiteral("console"));
    EXPECT_EQ(registry.pages()[1].id, QStringLiteral("flash"));
    EXPECT_EQ(registry.pages()[2].id, QStringLiteral("settings"));
    ASSERT_NE(registry.find(QStringLiteral("flash")), nullptr);
    EXPECT_EQ(registry.find(QStringLiteral("flash"))->order, 20);
    EXPECT_EQ(registry.find(QStringLiteral("nope")), nullptr);
    EXPECT_EQ(PageRegistry::title(registry.pages()[0]), QStringLiteral("Console"));
}

TEST(PageRegistry, DuplicatePageIsAProgrammingError) {
    PageRegistry registry;
    registry.add(page("console", 10));
    EXPECT_DEBUG_DEATH(registry.add(page("console", 5)), "duplicate page id");
    ASSERT_EQ(registry.pages().size(), 1U);
    EXPECT_EQ(registry.pages()[0].order, 10);
}

TEST(PageRegistry, SettingsSections) {
    PageRegistry registry;
    registry.addSettingsSection(SettingsSectionDescriptor{
        .id = QStringLiteral("jlink"), .titleSourceText = "J-Link", .qml = {}, .order = 30});
    registry.addSettingsSection(SettingsSectionDescriptor{
        .id = QStringLiteral("general"), .titleSourceText = "General", .qml = {}, .order = 10});
    ASSERT_EQ(registry.settingsSections().size(), 2U);
    EXPECT_EQ(registry.settingsSections()[0].id, QStringLiteral("general"));
    EXPECT_EQ(PageRegistry::title(registry.settingsSections()[1]), QStringLiteral("J-Link"));
    EXPECT_DEBUG_DEATH(
        registry.addSettingsSection(SettingsSectionDescriptor{
            .id = QStringLiteral("general"), .titleSourceText = "x", .qml = {}, .order = 1}),
        "duplicate section id");
    EXPECT_EQ(registry.settingsSections().size(), 2U);
}

} // namespace

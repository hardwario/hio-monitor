// AppViewModel: tab lifecycle and the 8-tab limit, restore without connect, toasts, theme, global
// pages, responsive rail (plan 0004, B1, B2; ADR-0043, ADR-0061).

#include "gui_test_support.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"

#include <QByteArray>
#include <QRect>
#include <QSettings>
#include <QSignalSpy>
#include <QSize>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QWindow>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <memory>

namespace {

using hio::gui::AppViewModel;
using hio::gui::DeviceTabViewModel;
using hio::gui::test::spin;
using hio::gui::test::waitFor;

class AppViewModelTest : public hio::gui::test::GuiTest {
protected:
    void writeRaw(const QString& key, const QVariant& value) const {
        QSettings raw(QString::fromStdU16String(settingsFile().u16string()), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"), 1);
        raw.setValue(key, value);
    }
    [[nodiscard]] QString readRaw(const QString& key) const {
        const QSettings raw(QString::fromStdU16String(settingsFile().u16string()),
                            QSettings::IniFormat);
        return raw.value(key).toString();
    }
};

TEST_F(AppViewModelTest, FirstStartOpensOneChooseDeviceTab) {
    const auto app = makeApp();
    ASSERT_EQ(app->tabs()->count(), 1);
    EXPECT_EQ(app->currentTab(), 0);
    DeviceTabViewModel* tab = app->currentTabViewModel();
    ASSERT_NE(tab, nullptr);
    EXPECT_EQ(tab->state(), DeviceTabViewModel::ChoosingDevice);
    EXPECT_EQ(tab->title(), QStringLiteral("New device"));
    EXPECT_EQ(tab->session(), nullptr);
}

TEST_F(AppViewModelTest, AtMostEightTabs) {
    const auto app = makeApp();
    QSignalSpy current(app.get(), &AppViewModel::currentTabChanged);
    for (int i = 1; i < AppViewModel::maxTabs(); ++i) {
        app->newTab();
    }
    EXPECT_EQ(app->tabs()->count(), 8);
    EXPECT_FALSE(app->tabs()->canAdd());
    EXPECT_EQ(app->currentTab(), 7);
    app->newTab();
    EXPECT_EQ(app->tabs()->count(), 8);
    EXPECT_GE(current.count(), 7);
}

TEST_F(AppViewModelTest, ClosingTabsAndTheLastOne) {
    const auto app = makeApp();
    app->newTab();
    app->newTab();
    ASSERT_EQ(app->tabs()->count(), 3);
    app->setCurrentTab(1);
    app->closeTab(1);
    EXPECT_EQ(app->tabs()->count(), 2);
    EXPECT_EQ(app->currentTab(), 1);
    app->closeTab(0);
    app->closeTab(0);
    // The last tab leaves one empty Choose device tab.
    ASSERT_EQ(app->tabs()->count(), 1);
    EXPECT_EQ(app->currentTabViewModel()->state(), DeviceTabViewModel::ChoosingDevice);
    spin(std::chrono::milliseconds(20)); // retired tabs are deleted on the next turn
}

TEST_F(AppViewModelTest, TabNavigation) {
    const auto app = makeApp();
    app->newTab();
    app->newTab();
    app->setCurrentTab(0);
    app->nextTab();
    EXPECT_EQ(app->currentTab(), 1);
    app->previousTab();
    app->previousTab();
    EXPECT_EQ(app->currentTab(), 2);
    app->setCurrentTab(17);
    EXPECT_EQ(app->currentTab(), 2);
}

TEST_F(AppViewModelTest, TabsAreSavedAndRestoredWithoutConnecting) {
    {
        const auto app = makeApp();
        ASSERT_TRUE(app->currentTabViewModel()->configure(QStringLiteral("chester"),
                                                          QStringLiteral("app"), 801000001));
        app->newTab();
        ASSERT_TRUE(app->currentTabViewModel()->configure(QStringLiteral("chester"),
                                                          QStringLiteral("lte"), 801000002));
        app->newTab(); // a Choose device tab is not saved
        app->setCurrentTab(1);
        app->saveState();
    }
    EXPECT_EQ(
        readRaw(QStringLiteral("ui/tabs")),
        QStringLiteral(R"({"current":1,"tabs":[{"profile":"chester","sn":801000001,)"
                       R"("target":"app"},{"profile":"chester","sn":801000002,"target":"lte"}]})"));
    const auto app = makeApp();
    ASSERT_EQ(app->tabs()->count(), 2);
    EXPECT_EQ(app->currentTab(), 1);
    for (int i = 0; i < 2; ++i) {
        const DeviceTabViewModel* tab = app->tabs()->at(i);
        EXPECT_EQ(tab->state(), DeviceTabViewModel::Disconnected);
        EXPECT_EQ(tab->session(), nullptr); // never connects at start
    }
    EXPECT_EQ(app->tabs()->at(0)->title(), QStringLiteral("CHESTER app · 801000001"));
    EXPECT_EQ(app->tabs()->at(1)->consoleTargetId(), QStringLiteral("lte"));
    spin(std::chrono::milliseconds(300));
    EXPECT_EQ(app->sessionRegistry().sessionCount(), 0U);
}

TEST_F(AppViewModelTest, RestoreCanBeTurnedOff) {
    writeRaw(
        QStringLiteral("ui/tabs"),
        QStringLiteral(R"({"current":0,"tabs":[{"profile":"chester","target":"app","sn":5}]})"));
    writeRaw(QStringLiteral("ui/restoreTabs"), false);
    const auto app = makeApp();
    ASSERT_EQ(app->tabs()->count(), 1);
    EXPECT_EQ(app->currentTabViewModel()->state(), DeviceTabViewModel::ChoosingDevice);
}

TEST_F(AppViewModelTest, InvalidTabListsAreIgnored) {
    const std::array invalid{
        R"(not json)",
        R"({"current":0,"tabs":[{"profile":"nope","target":"app","sn":5}]})",
        R"({"current":0,"tabs":[{"profile":"chester","target":"xyz","sn":5}]})",
        R"({"current":0,"tabs":[{"profile":"chester","target":"app","sn":0}]})",
        R"({"current":0,"tabs":[{"profile":"chester","target":"app","sn":5},{"profile":"chester","target":"app","sn":5}]})",
        R"({"current":0,"tabs":[1,2,3,4,5,6,7,8,9]})",
    };
    for (const char* text : invalid) {
        writeRaw(QStringLiteral("ui/tabs"), QString::fromUtf8(text));
        const auto app = makeApp();
        ASSERT_EQ(app->tabs()->count(), 1) << text;
        EXPECT_EQ(app->currentTabViewModel()->state(), DeviceTabViewModel::ChoosingDevice) << text;
    }
}

TEST_F(AppViewModelTest, ToastsAreBounded) {
    const auto app = makeApp();
    for (int i = 0; i < 5; ++i) {
        app->showToast(QStringLiteral("toast %1").arg(i));
    }
    EXPECT_EQ(app->toasts().size(), 3);
    EXPECT_EQ(app->toasts().front(), QStringLiteral("toast 0"));
    app->showToast(QString()); // ignored
    // After 4 s the first three expire and the queued ones follow.
    ASSERT_TRUE(waitFor([&] { return app->toasts().contains(QStringLiteral("toast 4")); },
                        std::chrono::milliseconds(6000)));
    EXPECT_LE(app->toasts().size(), 3);
}

TEST_F(AppViewModelTest, Theme) {
    const auto app = makeApp();
    QSignalSpy changed(app.get(), &AppViewModel::themeChanged);
    EXPECT_EQ(app->theme(), QStringLiteral("system"));
    app->setTheme(QStringLiteral("dark"));
    EXPECT_EQ(app->theme(), QStringLiteral("dark"));
    EXPECT_EQ(changed.count(), 1);
    app->setTheme(QStringLiteral("pink"));
    EXPECT_EQ(app->theme(), QStringLiteral("dark"));
    EXPECT_EQ(readRaw(QStringLiteral("ui/theme")), QStringLiteral("dark"));
}

TEST_F(AppViewModelTest, PagesAndGlobalPages) {
    const auto app = makeApp();
    const QVariantList pages = app->pages();
    ASSERT_EQ(pages.size(), 4);
    EXPECT_EQ(pages[0].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("console"));
    EXPECT_EQ(pages[1].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("flash"));
    EXPECT_EQ(pages[2].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("test"));
    EXPECT_EQ(pages[3].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("settings"));
    EXPECT_TRUE(pages[3].toMap().value(QStringLiteral("global")).toBool());

    ASSERT_NE(app->globalPageViewModel(QStringLiteral("settings")), nullptr);
    EXPECT_EQ(app->globalPageViewModel(QStringLiteral("settings"))->parent(), app.get());
    EXPECT_EQ(app->globalPageViewModel(QStringLiteral("console")), nullptr);
    app->openGlobalPage(QStringLiteral("console")); // not global
    EXPECT_TRUE(app->globalPage().isEmpty());
    app->openGlobalPage(QStringLiteral("settings"));
    EXPECT_EQ(app->globalPage(), QStringLiteral("settings"));
    app->showPage(QStringLiteral("console"));
    EXPECT_TRUE(app->globalPage().isEmpty());
}

TEST_F(AppViewModelTest, RailCollapsesBelow1100) {
    const auto app = makeApp();
    QSignalSpy changed(app.get(), &AppViewModel::railCompactChanged);
    app->setWindowWidth(1100);
    EXPECT_FALSE(app->railCompact());
    app->setWindowWidth(1099);
    EXPECT_TRUE(app->railCompact());
    app->setWindowWidth(960);
    EXPECT_TRUE(app->railCompact());
    EXPECT_EQ(changed.count(), 1);
}

TEST_F(AppViewModelTest, WindowGeometryIsRemembered) {
    {
        const auto app = makeApp();
        QWindow window;
        app->attachWindow(&window); // nothing stored yet: the window keeps its size
        window.setGeometry(QRect(40, 50, 1000, 700));
        app->saveState();
    }
    EXPECT_FALSE(readRaw(QStringLiteral("ui/windowGeometry")).isEmpty());
    const auto app = makeApp();
    QWindow window;
    window.resize(1280, 800);
    app->attachWindow(&window);
    EXPECT_EQ(window.geometry().size(), QSize(1000, 700));
    // A stored geometry below the minimum size is ignored.
    writeRaw(QStringLiteral("ui/windowGeometry"),
             QString::fromLatin1(QByteArray(R"({"v":1,"x":0,"y":0,"w":200,"h":100})").toBase64()));
    const auto other = makeApp();
    QWindow small;
    small.resize(1280, 800);
    other->attachWindow(&small);
    EXPECT_EQ(small.geometry().size(), QSize(1280, 800));
}

TEST_F(AppViewModelTest, QuitWithoutBusyPagesSavesState) {
    const auto app = makeApp();
    QSignalSpy needed(app.get(), &AppViewModel::quitConfirmationNeeded);
    EXPECT_TRUE(app->canQuit());
    EXPECT_EQ(needed.count(), 0);
}

} // namespace

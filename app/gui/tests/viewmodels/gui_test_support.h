#pragma once

// Fixture of the GUI view model tests: a temporary directory with the settings file, the probe
// locks, the J-Link library copies and console files; the fake J-Link library with a scenario; an
// AppViewModel factory.

#include "hio/core/settings.h"
#include "hio/device/session_registry.h"
#include "pages/builtin_pages.h"
#include "support/fake_jlink_scenario.h"
#include "support/test_page_view_model.h"
#include "viewmodels/app_view_model.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace hio::gui::test {

// Runs the event loop, including deferred deletes (deleteLater), until `done` or `timeout`.
inline bool waitFor(const std::function<bool()>& done,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(10000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return true;
}

inline void spin(std::chrono::milliseconds duration) {
    (void)waitFor([] { return false; }, duration);
}

class GuiTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(useFakeJLink(m_dir.path(), twoProbesScenario)); }
    void TearDown() override {
        qunsetenv("HIO_FAKE_JLINK_SCENARIO");
        qunsetenv("HIO_JLINK_LIBRARY");
    }

    void useScenario(const char* json) const { ASSERT_TRUE(useFakeJLink(m_dir.path(), json)); }

    [[nodiscard]] std::filesystem::path root() const { return {m_dir.path().toStdU16String()}; }
    [[nodiscard]] std::filesystem::path settingsFile() const { return root() / "Monitor.ini"; }

    [[nodiscard]] std::optional<Settings> openSettings() const {
        auto opened = Settings::openFile(settingsFile());
        if (!opened) {
            return std::nullopt;
        }
        return std::move(*opened);
    }

    [[nodiscard]] device::RegistryOptions registryOptions() const {
        return device::RegistryOptions{.instanceRoot = root() / "instances",
                                       .lockDirectory = root() / "locks",
                                       .consoleDirectory = root() / "console"};
    }

    // An AppViewModel with the built-in pages and the contract test page.
    [[nodiscard]] std::unique_ptr<AppViewModel> makeApp() const {
        return std::make_unique<AppViewModel>(
            AppOptions{.settings = openSettings(),
                       .registry = registryOptions(),
                       .scanInterval = std::chrono::milliseconds(200),
                       .registerExtensions = [](AppViewModel& app) {
                           registerBuiltInPages(app);
                           registerTestPage(app, QUrl(QStringLiteral("qrc:/TestPage.qml")));
                       }});
    }

private:
    QTemporaryDir m_dir;
};

} // namespace hio::gui::test

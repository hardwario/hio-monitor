#pragma once

// FirmwareServices - what the Flash page, the Settings page and the flash wizard (0010) share
// (plan/features/0006-m3-flash-settings.md, § Data flow and threading): the application's
// FirmwareLoader worker, the firmware cache and catalogs (ADR-0037; GUI thread, QtNetwork), the
// DependencyStatus, the setting keys of 0006 and the hooks into the shell (language switch, "is the
// Flash page of this tab on screen" for the success toast).
//
// Ownership: created by registerFlashAndSettingsPages() with AppViewModel as Qt parent, before the
// first tab exists. The page view models (children of the tabs and of AppViewModel) are deleted
// before it (AppViewModel's destructor deletes the tabs and the global page view models first), so
// no catalog operation and no loader request outlives it. GUI thread only.

#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "viewmodels/dependency_status.h"
#include "workers/firmware_loader.h"

#include <QObject>
#include <QString>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace hio::device {
struct DeviceProfile;
} // namespace hio::device

namespace hio::gui {

class DeviceTabViewModel;
class SettingsStore;

class FirmwareServices : public QObject {
    Q_OBJECT

public:
    // Setting keys of 0006 (ADR-0025).
    static inline const QString recentKey = QStringLiteral("flash/recent");
    static inline const QString jlinkPathKey = QStringLiteral("jlink/libraryPath");
    static inline const QString nrfUtilPathKey = QStringLiteral("nrfutil/path");

    struct Options {
        SettingsStore* settings = nullptr;    // outlives this object (AppViewModel)
        std::filesystem::path instanceRoot;   // J-Link library copies (ADR-0030)
        std::filesystem::path cacheDirectory; // firmware cache ("" = FirmwareCache default)
        // The locator options of the shell (environment, jlink/libraryPath, search paths).
        std::function<jlink::LocatorOptions()> jlinkOptions;
        // nRF Util: environment, nrfutil/path, PATH, install directories.
        std::function<nrfutil::LocatorOptions()> nrfUtilOptions;
        // True when the Flash page of `tab` is on screen (no toast then).
        std::function<bool(const DeviceTabViewModel* tab)> isFlashPageVisible;
    };

    explicit FirmwareServices(Options options, QObject* parent = nullptr);
    ~FirmwareServices() override;
    FirmwareServices(const FirmwareServices&) = delete;
    FirmwareServices& operator=(const FirmwareServices&) = delete;
    FirmwareServices(FirmwareServices&&) = delete;
    FirmwareServices& operator=(FirmwareServices&&) = delete;

    // Registers flash/recent, jlink/libraryPath and nrfutil/path.
    static void registerKeys(SettingsStore& store);

    [[nodiscard]] SettingsStore* settings() const { return m_options.settings; }
    [[nodiscard]] FirmwareLoader& loader() { return *m_loader; }
    [[nodiscard]] DependencyStatus* dependencies() const { return m_dependencies.get(); }
    // The catalog of `profile` (created on first use); nullptr when the profile has none.
    [[nodiscard]] firmware::FirmwareCatalog* catalog(const device::DeviceProfile& profile);
    [[nodiscard]] bool isFlashPageVisible(const DeviceTabViewModel* tab) const;

    // Called by the registration on a live language switch; view models re-read their texts.
    void retranslate();

signals:
    void languageChanged();

private:
    Options m_options;
    std::unique_ptr<FirmwareLoader> m_loader;
    std::unique_ptr<DependencyStatus> m_dependencies; // uses the loader
    std::unique_ptr<firmware::FirmwareCache> m_cache;
    // Declared after the cache: catalogs use it and are destroyed first.
    std::map<std::string, std::unique_ptr<firmware::FirmwareCatalog>, std::less<>> m_catalogs;
};

} // namespace hio::gui

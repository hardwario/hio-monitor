#include "pages/firmware_services.h"

#include "hio/device/profile.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "settings_store.h"
#include "viewmodels/dependency_status.h"
#include "workers/firmware_loader.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QtTypes>

#include <array>
#include <memory>
#include <string>
#include <utility>

namespace hio::gui {

namespace {

// flash/recent: at most 10 entries of {kind, source, time} (≤ 4096-character paths).
constexpr qsizetype maxRecentJsonLength = qsizetype{64} * 1024;
constexpr qsizetype maxPathLength = 4096;

} // namespace

FirmwareServices::FirmwareServices(Options options, QObject* parent)
    : QObject(parent), m_options(std::move(options)),
      m_loader(std::make_unique<FirmwareLoader>(m_options.instanceRoot)) {
    // No Qt parent: destroyed before the loader (member order); QML only reads it through a
    // property (C++ ownership).
    m_dependencies = std::make_unique<DependencyStatus>(
        *m_loader, m_options.settings,
        DependencyStatus::Hooks{.jlinkOptions = m_options.jlinkOptions,
                                .nrfUtilOptions = m_options.nrfUtilOptions});
    m_cache = std::make_unique<firmware::FirmwareCache>(
        m_options.cacheDirectory.empty() ? firmware::FirmwareCache::defaultDirectory()
                                         : m_options.cacheDirectory);
}

// Members in reverse order: catalogs, cache, dependency status, then the loader (joined).
FirmwareServices::~FirmwareServices() = default;

void FirmwareServices::registerKeys(SettingsStore& store) {
    const std::array keys{
        SettingKey{.path = recentKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(maxRecentJsonLength)},
        // Existing keys of M1/M2 (read by the backend locators), edited on the Settings page;
        // empty = automatic.
        SettingKey{.path = jlinkPathKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(maxPathLength)},
        SettingKey{.path = nrfUtilPathKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(maxPathLength)},
    };
    store.registerKeys(keys);
}

firmware::FirmwareCatalog* FirmwareServices::catalog(const device::DeviceProfile& profile) {
    if (profile.firmwareCatalogUrl.empty()) {
        return nullptr;
    }
    const auto found = m_catalogs.find(profile.id);
    if (found != m_catalogs.end()) {
        return found->second.get();
    }
    auto created = std::make_unique<firmware::FirmwareCatalog>(
        firmware::CatalogOptions{.baseUrl = firmware::catalogBaseUrl(profile.firmwareCatalogUrl)},
        m_cache.get());
    firmware::FirmwareCatalog* catalog = created.get();
    m_catalogs.emplace(std::string(profile.id), std::move(created));
    return catalog;
}

bool FirmwareServices::isFlashPageVisible(const DeviceTabViewModel* tab) const {
    return m_options.isFlashPageVisible && m_options.isFlashPageVisible(tab);
}

void FirmwareServices::retranslate() {
    m_dependencies->retranslate();
    languageChanged(); // signal
}

} // namespace hio::gui

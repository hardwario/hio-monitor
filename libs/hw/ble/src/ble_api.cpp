#include "hwio/hw/ble/ble_api.h"

#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "qt_ble_api.h"

#include <QLoggingCategory>
#include <QtEnvironmentVariables>
#include <QtLogging>

#include <memory>
#include <utility>

#ifdef HWIO_TEST_OVERRIDES
#include "fake/fake_ble_api.h"
#include "hwio/hw/ble/types.h"

#include <filesystem>
#endif

namespace hwio::ble {

std::unique_ptr<IBleApi> createBleApi() {
#ifdef HWIO_TEST_OVERRIDES
    // ADR-0087, ADR-0127: only in builds with HWIO_TEST_OVERRIDES.
    const QString scenario = qEnvironmentVariable("HWIO_FAKE_BLE_SCENARIO");
    if (!scenario.isEmpty()) {
        auto loaded = loadFakeScenario(std::filesystem::path(scenario.toStdU16String()));
        if (loaded) {
            return std::make_unique<FakeBleApi>(std::move(*loaded));
        }
        qCCritical(log::lcBle).noquote()
            << "HWIO_FAKE_BLE_SCENARIO:" << toQString(loaded.error().detail);
        FakeScenario none;
        none.adapter.state = AdapterState::NotFound;
        return std::make_unique<FakeBleApi>(std::move(none));
    }
#endif
    return std::make_unique<QtBleApi>();
}

} // namespace hwio::ble

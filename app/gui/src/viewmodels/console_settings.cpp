#include "viewmodels/console_settings.h"

#include "gui_log.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/completer.h"
#include "hio/device/console_file_sink.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"

#include <QDesktopServices>
#include <QDir>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QMetaType>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtCore/qnamespace.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <system_error>
#include <utility>

namespace hio::gui {

namespace {

const QString fontSizeKey = QStringLiteral("console/fontSize");
const QString timestampsKey = QStringLiteral("console/showTimestamps");
const QString wrapKey = QStringLiteral("console/wrapLines");
const QString splitKey = QStringLiteral("console/splitRatio");
const QString stackedKey = QStringLiteral("console/stacked");
const QString logToFileKey = QStringLiteral("console/logToFile");
// 0021 (ADR-0170): shared with the CLI console (device::suggestionsSetting, ...).
const QString suggestionsKey = toQString(device::suggestionsSetting.path);
const QString readCommandsKey = toQString(device::readCommandsOnConnectSetting.path);

} // namespace

ConsoleSettings::ConsoleSettings(SettingsStore* store, std::filesystem::path logFolder,
                                 QObject* parent)
    : QObject(parent), m_store(store), m_folder(std::move(logFolder)) {
    if (m_store) {
        connect(m_store, &SettingsStore::valueChanged, this, [this](const QString& key) {
            if (key.startsWith(QStringLiteral("console/"))) {
                changed(); // signal
            }
        });
    }
}

void ConsoleSettings::registerKeys(SettingsStore& store) {
    const std::array keys{
        SettingKey{.path = fontSizeKey,
                   .type = QMetaType::Int,
                   .defaultValue = defaultFont,
                   .validator = validators::intRange(minFont, maxFont)},
        SettingKey{
            .path = timestampsKey, .type = QMetaType::Bool, .defaultValue = false, .validator = {}},
        SettingKey{
            .path = wrapKey, .type = QMetaType::Bool, .defaultValue = false, .validator = {}},
        SettingKey{.path = splitKey,
                   .type = QMetaType::Int,
                   .defaultValue = 50,
                   .validator = validators::intRange(minSplit, maxSplit)},
        SettingKey{
            .path = stackedKey, .type = QMetaType::Bool, .defaultValue = false, .validator = {}},
        SettingKey{
            .path = logToFileKey, .type = QMetaType::Bool, .defaultValue = true, .validator = {}},
        SettingKey{.path = suggestionsKey,
                   .type = QMetaType::Bool,
                   .defaultValue = device::suggestionsSetting.defaultValue,
                   .validator = {}},
        SettingKey{.path = readCommandsKey,
                   .type = QMetaType::Bool,
                   .defaultValue = device::readCommandsOnConnectSetting.defaultValue,
                   .validator = {}},
    };
    store.registerKeys(keys);
}

ConsoleSettings* ConsoleSettings::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    AppViewModel* app = AppViewModel::create(qmlEngine, jsEngine);
    auto* instance = app != nullptr
                         ? app->findChild<ConsoleSettings*>(QString(), Qt::FindDirectChildrenOnly)
                         : nullptr;
    if (instance != nullptr) {
        QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
    qCWarning(lcGui) << "ConsoleSettings: no instance registered; using defaults";
    // Qt parent ownership: the engine owns this fallback instance.
    return new ConsoleSettings(nullptr, {}, qmlEngine); // NOLINT(cppcoreguidelines-owning-memory)
}

QVariant ConsoleSettings::read(const QString& key, const QVariant& fallback) const {
    if (!m_store || !m_store->isRegistered(key)) {
        return fallback;
    }
    return m_store->value(key);
}

void ConsoleSettings::write(const QString& key, const QVariant& value) {
    if (!m_store) {
        return;
    }
    if (const Status written = m_store->setValue(key, value); !written) {
        qCWarning(lcGui) << "Console setting not saved:" << key
                         << toQString(written.error().message);
        changed(); // signal: the controls show the stored value again
    }
}

bool ConsoleSettings::suggestions() const {
    return read(suggestionsKey, device::suggestionsSetting.defaultValue).toBool();
}

void ConsoleSettings::setSuggestions(bool enabled) {
    if (enabled != suggestions()) {
        write(suggestionsKey, enabled);
    }
}

bool ConsoleSettings::readCommandsOnConnect() const {
    return read(readCommandsKey, device::readCommandsOnConnectSetting.defaultValue).toBool();
}

void ConsoleSettings::setReadCommandsOnConnect(bool enabled) {
    if (enabled != readCommandsOnConnect()) {
        write(readCommandsKey, enabled);
    }
}

int ConsoleSettings::fontSize() const { return read(fontSizeKey, defaultFont).toInt(); }

void ConsoleSettings::setFontSize(int size) {
    size = std::clamp(size, minFont, maxFont);
    if (size != fontSize()) {
        write(fontSizeKey, size);
    }
}

bool ConsoleSettings::showTimestamps() const { return read(timestampsKey, false).toBool(); }

void ConsoleSettings::setShowTimestamps(bool show) {
    if (show != showTimestamps()) {
        write(timestampsKey, show);
    }
}

bool ConsoleSettings::wrapLines() const { return read(wrapKey, false).toBool(); }

void ConsoleSettings::setWrapLines(bool wrap) {
    if (wrap != wrapLines()) {
        write(wrapKey, wrap);
    }
}

int ConsoleSettings::splitRatio() const { return read(splitKey, 50).toInt(); }

void ConsoleSettings::setSplitRatio(int percent) {
    percent = std::clamp(percent, minSplit, maxSplit);
    if (percent != splitRatio()) {
        write(splitKey, percent);
    }
}

bool ConsoleSettings::stacked() const { return read(stackedKey, false).toBool(); }

void ConsoleSettings::setStacked(bool stacked) {
    if (stacked != this->stacked()) {
        write(stackedKey, stacked);
    }
}

bool ConsoleSettings::logToFile() const { return read(logToFileKey, true).toBool(); }

void ConsoleSettings::setLogToFile(bool log) {
    if (log != logToFile()) {
        write(logToFileKey, log);
    }
}

QString ConsoleSettings::logFolder() const {
    return QDir::toNativeSeparators(toQString(toUtf8(m_folder)));
}

int ConsoleSettings::retentionFiles() {
    return static_cast<int>(device::ConsoleFileSink::maxDefaultFiles);
}

int ConsoleSettings::retentionMegabytes() {
    return static_cast<int>(device::ConsoleFileSink::maxDefaultTotalBytes / (1024ULL * 1024));
}

void ConsoleSettings::increaseFontSize() { setFontSize(fontSize() + 1); }

void ConsoleSettings::decreaseFontSize() { setFontSize(fontSize() - 1); }

void ConsoleSettings::resetFontSize() { setFontSize(defaultFont); }

void ConsoleSettings::openLogFolder() const {
    if (m_folder.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::create_directories(m_folder, ignored);
    if (!openUrl(QUrl::fromLocalFile(toQString(toUtf8(m_folder))))) {
        qCWarning(lcGui) << "The console log folder could not be opened:" << logFolder();
    }
}

bool ConsoleSettings::openUrl(const QUrl& url) const {
    return m_opener ? m_opener(url) : QDesktopServices::openUrl(url);
}

} // namespace hio::gui

#pragma once

// ConsoleSettings - the console/* settings (plan/features/0005-m3-console.md, C5b; ADR-0025) for
// the console pages and the Console section of the Settings page (ConsoleSection.qml).
//
// Registers its keys with the SettingsStore (registerKeys) and exposes them as properties: the
// setters validate and write through the SettingsStore (the GUI's only settings writer), and
// `changed` follows SettingsStore::valueChanged, so every console page and the Settings section
// show the same values.
//
// Ownership: one instance, a child of AppViewModel (created by registerConsolePage). QML reaches it
// as the singleton ConsoleSettings (create() finds the instance). GUI thread only.

#include "settings_store.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtQmlIntegration>

#include <filesystem>
#include <functional>
#include <utility>

class QJSEngine;
class QQmlEngine;

namespace hio::gui {

// Opens a file or folder with the desktop (QDesktopServices::openUrl); tests replace it.
using UrlOpener = std::function<bool(const QUrl&)>;

class ConsoleSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(bool showTimestamps READ showTimestamps WRITE setShowTimestamps NOTIFY changed)
    Q_PROPERTY(bool wrapLines READ wrapLines WRITE setWrapLines NOTIFY changed)
    Q_PROPERTY(int splitRatio READ splitRatio WRITE setSplitRatio NOTIFY changed)
    Q_PROPERTY(bool stacked READ stacked WRITE setStacked NOTIFY changed)
    Q_PROPERTY(bool logToFile READ logToFile WRITE setLogToFile NOTIFY changed)
    // 0021 (ADR-0170): the suggestion list while typing; `help` on connect (else on first use).
    Q_PROPERTY(bool suggestions READ suggestions WRITE setSuggestions NOTIFY changed)
    Q_PROPERTY(bool readCommandsOnConnect READ readCommandsOnConnect WRITE setReadCommandsOnConnect
                   NOTIFY changed)
    Q_PROPERTY(int minFontSize READ minFontSize CONSTANT)
    Q_PROPERTY(int maxFontSize READ maxFontSize CONSTANT)
    Q_PROPERTY(QString logFolder READ logFolder CONSTANT)
    Q_PROPERTY(int retentionFiles READ retentionFiles CONSTANT)
    Q_PROPERTY(int retentionMegabytes READ retentionMegabytes CONSTANT)

public:
    static constexpr int minFont = 8;
    static constexpr int maxFont = 24;
    static constexpr int defaultFont = 10;
    static constexpr int minSplit = 20;
    static constexpr int maxSplit = 80;

    // `store` may be null (QML fallback instance: defaults, writes ignored).
    ConsoleSettings(SettingsStore* store, std::filesystem::path logFolder, QObject* parent);

    // The console/* keys (fontSize, showTimestamps, wrapLines, splitRatio, stacked, logToFile;
    // 0021: suggestions, readCommandsOnConnect).
    static void registerKeys(SettingsStore& store);

    // QML singleton factory: the instance created by registerConsolePage (a child of
    // AppViewModel); a default instance owned by the engine otherwise.
    static ConsoleSettings* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] int fontSize() const;
    void setFontSize(int size);
    [[nodiscard]] bool showTimestamps() const;
    void setShowTimestamps(bool show);
    [[nodiscard]] bool wrapLines() const;
    void setWrapLines(bool wrap);
    [[nodiscard]] int splitRatio() const;
    void setSplitRatio(int percent);
    [[nodiscard]] bool stacked() const;
    void setStacked(bool stacked);
    [[nodiscard]] bool logToFile() const;
    void setLogToFile(bool log);
    [[nodiscard]] bool suggestions() const;
    void setSuggestions(bool enabled);
    [[nodiscard]] bool readCommandsOnConnect() const;
    void setReadCommandsOnConnect(bool enabled);
    [[nodiscard]] static int minFontSize() { return minFont; }
    [[nodiscard]] static int maxFontSize() { return maxFont; }
    [[nodiscard]] QString logFolder() const;
    [[nodiscard]] static int retentionFiles();
    [[nodiscard]] static int retentionMegabytes();

    // Ctrl+= / Ctrl+- / Ctrl+0 (ADR-0051).
    Q_INVOKABLE void increaseFontSize();
    Q_INVOKABLE void decreaseFontSize();
    Q_INVOKABLE void resetFontSize();
    // Opens the console log folder in the file manager (created when missing).
    Q_INVOKABLE void openLogFolder() const;

    [[nodiscard]] const std::filesystem::path& logFolderPath() const noexcept { return m_folder; }
    // Opens `url` with the desktop (or the test opener); false when that failed.
    [[nodiscard]] bool openUrl(const QUrl& url) const;
    void setUrlOpener(UrlOpener opener) { m_opener = std::move(opener); }

signals:
    void changed();

private:
    [[nodiscard]] QVariant read(const QString& key, const QVariant& fallback) const;
    void write(const QString& key, const QVariant& value);

    QPointer<SettingsStore> m_store; // non-owning; AppViewModel's child, outlives this object
    std::filesystem::path m_folder;
    UrlOpener m_opener;
};

} // namespace hio::gui

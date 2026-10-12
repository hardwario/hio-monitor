#pragma once

// GUI language switching (ADR-0027).
//
// Owns the translators for the project catalog (":/i18n/hio_<lang>.qm") and Qt's own catalogs
// (qtbase_<lang>, qtdeclarative_<lang> from QLibraryInfo::TranslationsPath; missing ones are
// logged, not fatal). The setting "ui/language" is "system" (default) | a code from
// hio::i18n::supportedLanguages; "system" follows QLocale::system().uiLanguages() and falls back
// to English. apply() switches live: translators, QLocale::setDefault, QQmlEngine::retranslate().
//
// The setting is read and written through the SettingsStore (the GUI's only settings writer,
// plan/features/0004); AppViewModel registers the key.
//
// Ownership: one instance per GUI process, created in main() on the stack after AppViewModel (it
// keeps a non-owning pointer to AppViewModel's SettingsStore). It lives on the GUI thread.

#include "hio/core/result.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTranslator>

#include <memory>
#include <vector>

class QQmlEngine;

namespace hio::gui {

class SettingsStore;

class LanguageManager : public QObject {
    Q_OBJECT

public:
    // `settings` may be null (tests): the choice is then not persisted. Non-owning; must outlive
    // this object.
    explicit LanguageManager(SettingsStore* settings, QObject* parent = nullptr);
    ~LanguageManager() override;
    LanguageManager(const LanguageManager&) = delete;
    LanguageManager& operator=(const LanguageManager&) = delete;
    LanguageManager(LanguageManager&&) = delete;
    LanguageManager& operator=(LanguageManager&&) = delete;

    // The chosen setting: "system", "en", "cs", ... (initially the stored value).
    [[nodiscard]] QString language() const { return m_language; }
    // The language actually in use ("en", "cs", ...).
    [[nodiscard]] QString effectiveLanguage() const { return m_effectiveLanguage; }

    // Engine to retranslate on a switch (non-owning; may be null, e.g. before QML is loaded).
    void setEngine(QQmlEngine* engine);

    // Switches the language and stores the choice. Errors: usage.invalid_argument for an
    // unknown code; a failure to store the setting is only logged.
    [[nodiscard]] Status apply(const QString& code);

    // Picks the first supported language from a uiLanguages() list ("cs-CZ", "en-US", ...).
    [[nodiscard]] static QString resolveSystemLanguage(const QStringList& uiLanguages);
    [[nodiscard]] static bool isSupported(const QString& code);

signals:
    void languageChanged();

private:
    void installTranslators(const QString& language);
    void removeTranslators();

    QPointer<SettingsStore> m_settings; // non-owning
    QPointer<QQmlEngine> m_engine;
    std::vector<std::unique_ptr<QTranslator>> m_translators; // installed, removed in order
    QString m_language;
    QString m_effectiveLanguage;
};

} // namespace hio::gui

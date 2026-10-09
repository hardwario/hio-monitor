#pragma once

// View model of the main window (ADR-0003: no logic in QML).
//
// Created by the QML engine as a singleton (QML_SINGLETON, default constructor). It finds the
// process's LanguageManager as a direct child of the application object (created in main(), see
// language_manager.h) and forwards language changes to it. Lives on the GUI thread.

#include "hio/core/version.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration>

namespace hio::gui {

class LanguageManager;

class AppViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildInfo READ buildInfo CONSTANT)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)

public:
    explicit AppViewModel(QObject* parent = nullptr);

    [[nodiscard]] QString version() const;
    // Technical build information (version, Qt, compiler); not translated.
    [[nodiscard]] QString buildInfo() const;
    // The language setting: "system", "en", "cs", ...
    [[nodiscard]] QString language() const;
    // [{code: "system", nativeName: ""}, {code: "en", nativeName: "English"}, ...]. The QML shows
    // a translated "System" for the empty native name.
    [[nodiscard]] QVariantList availableLanguages() const;

    // Switches the language live and stores the choice; unknown codes are logged and ignored.
    Q_INVOKABLE void setLanguage(const QString& code);

signals:
    void languageChanged();

private:
    QPointer<LanguageManager> m_languageManager; // non-owning, owned by main()
    BuildInfo m_buildInfo;
    QVariantList m_availableLanguages;
};

} // namespace hio::gui

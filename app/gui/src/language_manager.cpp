#include "language_manager.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/i18n.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTranslator>
#include <QVariant>
#include <QtContainerFwd>

#include <algorithm>
#include <expected>
#include <memory>
#include <utility>

namespace hio::gui {

namespace {

const QString systemLanguage = QStringLiteral("system");

const QString languageKey = QStringLiteral("ui/language");

} // namespace

LanguageManager::LanguageManager(SettingsStore* settings, QObject* parent)
    : QObject(parent), m_settings(settings), m_language(systemLanguage) {
    if (m_settings) {
        const QString stored = m_settings->value(languageKey).toString();
        if (stored == systemLanguage || isSupported(stored)) {
            m_language = stored;
        } else {
            qCWarning(lcGui) << "Unknown ui/language" << stored << "in the settings; using system";
        }
    }
    m_effectiveLanguage = m_language == systemLanguage
                              ? resolveSystemLanguage(QLocale::system().uiLanguages())
                              : m_language;
    installTranslators(m_effectiveLanguage);
    qCInfo(lcGui) << "Language:" << m_language << "->" << m_effectiveLanguage;
}

LanguageManager::~LanguageManager() { removeTranslators(); }

void LanguageManager::setEngine(QQmlEngine* engine) { m_engine = engine; }

Status LanguageManager::apply(const QString& code) {
    if (code != systemLanguage && !isSupported(code)) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         "unknown language code '" + toStdString(code) + "'"));
    }
    const QString effective =
        code == systemLanguage ? resolveSystemLanguage(QLocale::system().uiLanguages()) : code;
    const bool languageChangedNow = code != m_language;
    m_language = code;

    if (effective != m_effectiveLanguage) {
        m_effectiveLanguage = effective;
        removeTranslators();
        installTranslators(effective);
        if (m_engine) {
            m_engine->retranslate();
        }
        qCInfo(lcGui) << "Language switched to" << m_language << "->" << m_effectiveLanguage;
    }

    if (m_settings) {
        if (const Status stored = m_settings->setValue(languageKey, code); !stored) {
            qCWarning(lcGui) << "The language choice could not be saved:"
                             << toQString(stored.error().message)
                             << toQString(stored.error().detail);
        }
    }

    if (languageChangedNow) {
        languageChanged(); // signal
    }
    return {};
}

QString LanguageManager::resolveSystemLanguage(const QStringList& uiLanguages) {
    for (const QString& uiLanguage : uiLanguages) {
        // "cs-CZ", "cs_CZ", "cs" -> "cs"
        const QString code =
            uiLanguage.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
        if (isSupported(code)) {
            return code;
        }
    }
    return QStringLiteral("en");
}

bool LanguageManager::isSupported(const QString& code) {
    return std::ranges::any_of(i18n::supportedLanguages, [&code](const i18n::LanguageInfo& info) {
        return code == toQString(info.code);
    });
}

void LanguageManager::installTranslators(const QString& language) {
    // Project catalog first (embedded, prefix /i18n), then Qt's own catalogs. English needs the
    // project catalog for plural forms only and has no Qt catalogs.
    QLocale::setDefault(QLocale(language));
    auto project = std::make_unique<QTranslator>();
    const QString projectFile = QStringLiteral(":/i18n/hio_%1.qm").arg(language);
    if (project->load(projectFile)) {
        QCoreApplication::installTranslator(project.get());
        m_translators.push_back(std::move(project));
    } else {
        qCWarning(lcGui) << "Missing translation catalog" << projectFile;
    }

    if (language == QStringLiteral("en")) {
        return;
    }
    const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    for (const QString& catalog : {QStringLiteral("qtbase"), QStringLiteral("qtdeclarative")}) {
        auto translator = std::make_unique<QTranslator>();
        const QString name = catalog + QLatin1Char('_') + language;
        if (translator->load(name, qtDir)) {
            QCoreApplication::installTranslator(translator.get());
            m_translators.push_back(std::move(translator));
        } else {
            qCInfo(lcGui) << "Qt translation catalog" << name << "not found in" << qtDir;
        }
    }
}

void LanguageManager::removeTranslators() {
    for (const std::unique_ptr<QTranslator>& translator : m_translators) {
        QCoreApplication::removeTranslator(translator.get());
    }
    m_translators.clear();
}

} // namespace hio::gui

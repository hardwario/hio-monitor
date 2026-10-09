#include "viewmodels/app_view_model.h"

#include "gui_log.h"
#include "hio/core/i18n.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "language_manager.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtCore/qnamespace.h>

namespace hio::gui {

AppViewModel::AppViewModel(QObject* parent)
    : QObject(parent), m_languageManager(QCoreApplication::instance()->findChild<LanguageManager*>(
                           QString(), Qt::FindDirectChildrenOnly)),
      m_buildInfo(hio::buildInfo()) {
    m_availableLanguages.append(QVariantMap{{QStringLiteral("code"), QStringLiteral("system")},
                                            {QStringLiteral("nativeName"), QString()}});
    for (const i18n::LanguageInfo& info : i18n::supportedLanguages) {
        m_availableLanguages.append(
            QVariantMap{{QStringLiteral("code"), toQString(info.code)},
                        {QStringLiteral("nativeName"), toQString(info.nativeName)}});
    }
    if (m_languageManager) {
        connect(m_languageManager, &LanguageManager::languageChanged, this,
                &AppViewModel::languageChanged);
    } else {
        qCWarning(lcGui) << "AppViewModel: no LanguageManager; language switching is disabled";
    }
}

QString AppViewModel::version() const { return toQString(m_buildInfo.version); }

QString AppViewModel::buildInfo() const {
    return QStringLiteral("%1 · Qt %2 · %3")
        .arg(toQString(m_buildInfo.version), toQString(m_buildInfo.qtRuntime),
             toQString(m_buildInfo.compiler));
}

QString AppViewModel::language() const {
    return m_languageManager ? m_languageManager->language() : QStringLiteral("system");
}

QVariantList AppViewModel::availableLanguages() const { return m_availableLanguages; }

void AppViewModel::setLanguage(const QString& code) {
    if (!m_languageManager) {
        qCWarning(lcGui) << "setLanguage" << code << "ignored: no LanguageManager";
        return;
    }
    if (const Status status = m_languageManager->apply(code); !status) {
        qCWarning(lcGui) << "setLanguage" << code << "failed:" << toQString(status.error().message)
                         << toQString(status.error().detail);
    }
}

} // namespace hio::gui

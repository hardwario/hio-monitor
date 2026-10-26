#include "wizard/wizard.h"

#include <QString>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantMap>
#include <QtContainerFwd>

namespace hio::gui {

QVariantMap WizardStep::toMap() const {
    return QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), static_cast<int>(kind)},
        {QStringLiteral("title"), title},
        {QStringLiteral("heading"), heading},
        {QStringLiteral("body"), body},
        {QStringLiteral("illustration"), illustration},
        {QStringLiteral("embedded"), embedded},
        {QStringLiteral("commands"), commands},
        {QStringLiteral("choices"), choices},
        {QStringLiteral("primaryText"), primaryText},
        {QStringLiteral("showInStepList"), showInStepList},
        {QStringLiteral("linkText"), linkText},
        {QStringLiteral("linkUrl"), linkUrl},
        {QStringLiteral("actions"), actions},
    };
}

} // namespace hio::gui

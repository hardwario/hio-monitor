#include "hwio/core/app_identity.h"

#include "hwio/core/json.h"
#include "hwio/core/version.h"

#include <QCoreApplication>
#include <QString>
#include <QStringLiteral>

namespace hwio {

void initApplicationIdentity() {
    QCoreApplication::setOrganizationName(QStringLiteral("HARDWARIO"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("hardwario.com"));
    QCoreApplication::setApplicationName(QStringLiteral("Monitor"));
    QCoreApplication::setApplicationVersion(toQString(buildInfo().version));
}

} // namespace hwio

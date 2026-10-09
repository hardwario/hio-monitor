#include "hio/core/app_identity.h"

#include "hio/core/json.h"
#include "hio/core/version.h"

#include <QCoreApplication>
#include <QString>

namespace hio {

void initApplicationIdentity() {
    QCoreApplication::setOrganizationName(QStringLiteral("HARDWARIO"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("hardwario.com"));
    QCoreApplication::setApplicationName(QStringLiteral("Monitor"));
    QCoreApplication::setApplicationVersion(toQString(buildInfo().version));
}

} // namespace hio

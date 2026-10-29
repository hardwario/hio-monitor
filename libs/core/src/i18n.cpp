#include "hwio/core/i18n.h"

#include <QCoreApplication>
#include <QString>

#include <string>

namespace hwio::i18n {

std::string tr(const char* context, const char* sourceText, const char* disambiguation, int n) {
    return QCoreApplication::translate(context, sourceText, disambiguation, n).toStdString();
}

} // namespace hwio::i18n

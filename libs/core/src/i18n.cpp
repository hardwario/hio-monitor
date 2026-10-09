#include "hio/core/i18n.h"

#include <QCoreApplication>
#include <QString>

#include <string>

namespace hio::i18n {

std::string tr(const char* context, const char* sourceText, const char* disambiguation, int n) {
    return QCoreApplication::translate(context, sourceText, disambiguation, n).toStdString();
}

} // namespace hio::i18n

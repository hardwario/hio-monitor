#include "cli_log.h"

#include <QByteArrayView>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

#include <exception>
#include <string_view>

namespace hio::cli {

Q_LOGGING_CATEGORY(lcCli, "hio.cli")

void logThreadException(std::string_view thread) noexcept {
    try {
        QString what = QStringLiteral("unknown exception");
        if (const std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception& error) {
                what = QString::fromUtf8(error.what());
            } catch (...) { // NOLINT(bugprone-empty-catch): not a std::exception, "unknown"
            }
        }
        qCCritical(lcCli).noquote()
            << "Exception in the" << QString::fromUtf8(thread) << "thread:" << what;
    } catch (...) { // NOLINT(bugprone-empty-catch): logging failed; nothing else can be done
    }
}

} // namespace hio::cli

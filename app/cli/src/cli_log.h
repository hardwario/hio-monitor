#pragma once

// Logging category of the CLI (ADR-0020).

#include <QLoggingCategory>

#include <string_view>

namespace hio::cli {

Q_DECLARE_LOGGING_CATEGORY(lcCli) // "hio.cli"

// Logs (critical) that an exception ended the body of a CLI thread `thread`; never throws. The
// thread bodies catch everything (review R27): no exception may escape a thread.
void logThreadException(std::string_view thread) noexcept;

} // namespace hio::cli

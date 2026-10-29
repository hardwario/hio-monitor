#include "exit_codes.h"

#include "hwio/core/error.h"

namespace hwio::cli {

int exitCodeFor(ErrorCategory category) noexcept {
    switch (category) {
    case ErrorCategory::Internal:
        return 1;
    case ErrorCategory::Usage:
        return 2;
    case ErrorCategory::Io:
        return 3;
    case ErrorCategory::Dependency:
        return 4;
    case ErrorCategory::Probe:
        return 5;
    case ErrorCategory::Target:
        return 6;
    case ErrorCategory::Firmware:
        return 7;
    case ErrorCategory::Network:
        return 8;
    case ErrorCategory::Timeout:
        return 9;
    case ErrorCategory::Cancelled:
        return 130;
    }
    return 1;
}

} // namespace hwio::cli

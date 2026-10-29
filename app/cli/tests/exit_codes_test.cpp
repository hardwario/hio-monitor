#include "exit_codes.h"
#include "hwio/core/error.h"

#include <gtest/gtest.h>

namespace {

using hwio::ErrorCategory;
using hwio::cli::exitCodeFor;

// ADR-0024 / plan/0006-cli.md: public contract, never change.
TEST(ExitCodes, MatchTheDocumentedTable) {
    EXPECT_EQ(hwio::cli::exitSuccess, 0);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Internal), 1);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Usage), 2);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Io), 3);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Dependency), 4);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Probe), 5);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Target), 6);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Firmware), 7);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Network), 8);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Timeout), 9);
    EXPECT_EQ(exitCodeFor(ErrorCategory::Cancelled), 130);
}

TEST(ExitCodes, EveryErrorCodeMapsToNonZero) {
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        EXPECT_NE(exitCodeFor(info.category), 0) << info.id;
    }
}

} // namespace

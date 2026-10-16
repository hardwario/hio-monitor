#include "chester_commands.h"
#include "cli_app.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/version.h"
#include "output.h"

#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>

#include <chrono>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace {

using hio::cli::CliApp;
using hio::cli::Context;
using hio::cli::ParseOutcome;

TEST(CliApp, NoArgumentsShowsHelp) {
    CliApp cli;
    const auto parsed = cli.parse({});
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, ParseOutcome::ShowHelp);
}

TEST(CliApp, HelpDoesNotListSelftest) {
    CliApp cli;
    const auto parsed = cli.parse({"--help"});
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, ParseOutcome::ShowHelp);
    const std::string help = cli.helpText();
    EXPECT_NE(help.find("--json"), std::string::npos);
    EXPECT_NE(help.find("--log-level"), std::string::npos);
    EXPECT_EQ(help.find("_selftest"), std::string::npos);
}

TEST(CliApp, VersionData) {
    CliApp cli;
    const auto parsed = cli.parse({"--version"});
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, ParseOutcome::Run);
    const auto result = cli.run(Context{});
    ASSERT_TRUE(result.has_value());
    const std::string version(hio::buildInfo().version);
    EXPECT_EQ(result->humanText, "hio " + version);
    const QJsonObject data = result->data.toObject();
    EXPECT_EQ(data.value(QStringLiteral("name")).toString(), QStringLiteral("hio"));
    EXPECT_EQ(data.value(QStringLiteral("version")).toString(), hio::toQString(version));
    EXPECT_TRUE(data.contains(QStringLiteral("gitHash")));
    EXPECT_TRUE(data.contains(QStringLiteral("qt")));
    EXPECT_TRUE(data.contains(QStringLiteral("compiler")));
}

TEST(CliApp, GlobalOptions) {
    CliApp cli;
    ASSERT_TRUE(cli.parse({"--json", "--log-level", "success", "--version"}).has_value());
    EXPECT_TRUE(cli.options().json);
    EXPECT_TRUE(cli.options().version);
    EXPECT_EQ(cli.options().logLevel, hio::log::Level::Info);
}

TEST(CliApp, DefaultLogLevelIsCritical) {
    CliApp cli;
    ASSERT_TRUE(cli.parse({"--version"}).has_value());
    EXPECT_FALSE(cli.options().json);
    EXPECT_EQ(cli.options().logLevel, hio::log::Level::Critical);
}

TEST(CliApp, InvalidLogLevelIsUsageError) {
    CliApp cli;
    const auto parsed = cli.parse({"--log-level", "loud", "--version"});
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, hio::ErrorCode::UsageInvalidArgument);
    EXPECT_EQ(parsed.error().suggestion, "Run `hio --help`.");
    EXPECT_TRUE(parsed.error().cause.empty());
}

TEST(CliApp, UnknownOptionIsUsageError) {
    CliApp cli;
    const auto parsed = cli.parse({"--bogus"});
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, hio::ErrorCode::UsageInvalidArgument);
    EXPECT_NE(parsed.error().message.find("--bogus"), std::string::npos);
}

// ADR-0090 / review R49: --jlink-speed is `auto` (the default) or a fixed speed of 5-50 000 kHz,
// on the `chester app` group and on its subcommands; anything else is a usage error at parse time.
TEST(CliApp, JLinkSpeedAcceptsAutoOrKiloHertz) {
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"chester", "app", "command", "--jlink-speed", "auto", "version"},
             {"chester", "app", "command", "--jlink-speed", "2000", "version"},
             {"chester", "app", "command", "--jlink-speed", "5", "version"},
             {"chester", "app", "command", "--jlink-speed", "50000", "version"},
             {"chester", "app", "--jlink-speed", "auto", "console"},
             {"chester", "app", "console", "--jlink-speed", "4000"},
             {"chester", "app", "command", "version"}}) {
        CliApp cli;
        const auto parsed = cli.parse(args);
        ASSERT_TRUE(parsed.has_value()) << args.at(3) << ": " << parsed.error().message;
        EXPECT_EQ(*parsed, ParseOutcome::Run);
    }
}

TEST(CliApp, InvalidJLinkSpeedIsUsageError) {
    for (const std::string speed : {"fast", "1", "4", "50001", "", "2000k", "-5", "auto2"}) {
        CliApp cli;
        const auto parsed =
            cli.parse({"chester", "app", "command", "--jlink-speed", speed, "version"});
        ASSERT_FALSE(parsed.has_value()) << speed;
        EXPECT_EQ(parsed.error().code, hio::ErrorCode::UsageInvalidArgument) << speed;
        EXPECT_NE(parsed.error().message.find("auto or between 5 and 50000 kHz"), std::string::npos)
            << parsed.error().message;
    }
}

TEST(ParseJLinkSpeed, AutoIsNulloptAndKiloHertzIsFixed) {
    const auto automatic = hio::cli::parseJLinkSpeed("auto");
    ASSERT_TRUE(automatic.has_value());
    EXPECT_FALSE(automatic->has_value());
    const auto fixed = hio::cli::parseJLinkSpeed("2000");
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->value_or(0), 2000);
    const auto fast = hio::cli::parseJLinkSpeed("fast");
    ASSERT_FALSE(fast.has_value());
    EXPECT_EQ(fast.error().code, hio::ErrorCode::UsageInvalidArgument);
    EXPECT_FALSE(hio::cli::parseJLinkSpeed("1").has_value());
}

TEST(CliApp, SelftestErrorReturnsTableError) {
    for (const hio::ErrorInfo& info : hio::errorTable) {
        CliApp cli;
        ASSERT_TRUE(cli.parse({"_selftest", "error", std::string(info.id)}).has_value());
        const auto result = cli.run(Context{});
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, info.code);
        EXPECT_EQ(result.error().message, hio::makeError(info.code).message);
    }
}

TEST(CliApp, SelftestUnknownIdIsUsageError) {
    CliApp cli;
    ASSERT_TRUE(cli.parse({"_selftest", "error", "no.such_id"}).has_value());
    const auto result = cli.run(Context{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::UsageInvalidArgument);
}

TEST(CliApp, SelftestErrorNeedsId) {
    CliApp cli;
    const auto parsed = cli.parse({"_selftest", "error"});
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, hio::ErrorCode::UsageInvalidArgument);
}

TEST(CliApp, SelftestWaitEndsWhenStopped) {
    CliApp cli;
    ASSERT_TRUE(cli.parse({"_selftest", "wait"}).has_value());
    std::stop_source stop;
    std::jthread stopper([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stop.request_stop();
    });
    const auto result = cli.run(Context{.mode = hio::cli::OutputMode::Human,
                                        .stop = stop.get_token(),
                                        .output = nullptr,
                                        .logFile = {}});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::Cancelled);
}

TEST(JsonRequested, FindsFlagBeforeSeparator) {
    EXPECT_TRUE(hio::cli::jsonRequested({"--bogus", "--json"}));
    EXPECT_FALSE(hio::cli::jsonRequested({"--bogus"}));
    EXPECT_FALSE(hio::cli::jsonRequested({"--", "--json"}));
}

} // namespace

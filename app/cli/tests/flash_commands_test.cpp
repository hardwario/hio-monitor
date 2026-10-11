// CLI pieces of the M2 commands (plan/features/0003, F1): progress rendering, confirmations and
// the `chester app flash` argument rules.

#include "cli_app.h"
#include "confirm.h"
#include "flash_commands.h"
#include "hio/core/error.h"
#include "hio/device/operation.h"
#include "output.h"
#include "progress_ui.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::cli::Context;
using hio::cli::OutputMode;
using hio::cli::ProgressUi;

TEST(ProgressUiTest, BarLine) {
    EXPECT_EQ(ProgressUi::barLine("Programming", 50, 100, 1, 1),
              "Programming     [###############               ]  50 %");
    EXPECT_EQ(ProgressUi::barLine("Modem firmware", 100, 100, 1, 2),
              "[1/2] Modem firmware  [##############################] 100 %");
    EXPECT_EQ(ProgressUi::barLine("Connecting", 0, 0, 1, 1), "Connecting      ...");
    EXPECT_EQ(ProgressUi::label("switchingTarget"), "Switching target");
}

TEST(ProgressUiTest, PlainLinesOncePerPhase) {
    const Context context{.mode = OutputMode::Human, .stop = {}, .output = nullptr, .logFile = {}};
    std::ostringstream err;
    {
        ProgressUi ui(context, err, false);
        ui.progress("erasing", 0, 0);
        ui.progress("programming", 10, 100);
        ui.progress("programming", 90, 100);
        ui.info("Target: nRF52840 (app)");
    }
    EXPECT_EQ(err.str(), "Erasing...\nProgramming...\nTarget: nRF52840 (app)\n");
}

TEST(ProgressUiTest, BarsOnATerminal) {
    const Context context{.mode = OutputMode::Human, .stop = {}, .output = nullptr, .logFile = {}};
    std::ostringstream err;
    {
        ProgressUi ui(context, err, true);
        ui.progress("programming", 0, 100);
        ui.progress("programming", 0, 100); // unchanged: not redrawn
        ui.progress("programming", 100, 100);
        ui.progress("verifying", 100, 100);
    }
    const std::string text = err.str();
    EXPECT_EQ(std::count(text.begin(), text.end(), '\r'), 3);
    EXPECT_TRUE(text.ends_with("100 %\n"));
}

TEST(ProgressUiTest, JsonEvents) {
    std::ostringstream out;
    std::ostringstream err;
    hio::cli::Output output(OutputMode::Json, out, err, false);
    const Context context{.mode = OutputMode::Json, .stop = {}, .output = &output, .logFile = {}};
    ProgressUi ui(context, err, true);
    hio::device::OperationProgress progress;
    progress.phase = hio::device::ProgramPhase::Programming;
    progress.done = 204800;
    progress.total = 421888;
    progress.step = 1;
    progress.steps = 2;
    ui.progress(progress);
    ui.progress(progress); // identical: skipped
    ui.warning(hio::makeError(ErrorCode::IoWriteFailed, "cache"));
    ui.info("not in JSON");
    EXPECT_EQ(
        out.str(),
        R"({"data":{"done":204800,"phase":"programming","step":1,"steps":2,"total":421888,"type":"progress"},"ok":true,"schema":1})"
        "\n"
        R"({"data":{"error":{"category":"io","cause":"The disk is full or read-only, or you do not have permission to write.","code":"io.write_failed","detail":"cache","message":"The file could not be written.","suggestion":"Check the path, the free disk space and the permissions (see Details).","wizard":null},"type":"warning"},"ok":true,"schema":1})"
        "\n");
    EXPECT_TRUE(err.str().empty());
}

TEST(Confirm, Rules) {
    const Context human{.mode = OutputMode::Human, .stop = {}, .output = nullptr, .logFile = {}};
    const Context json{.mode = OutputMode::Json, .stop = {}, .output = nullptr, .logFile = {}};
    const std::string question = "This erases all flash.\nContinue? [y/N] ";
    EXPECT_TRUE(hio::cli::confirmDestructive(human, true, question));
    const auto refused = hio::cli::confirmDestructive(human, false, question, {}, false);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::UsageConfirmationRequired);
    EXPECT_EQ(refused.error().detail, "This erases all flash.");
    EXPECT_FALSE(hio::cli::confirmDestructive(json, false, question));
    std::string asked;
    EXPECT_TRUE(hio::cli::confirmDestructive(
        human, false, question,
        [&asked](const std::string& q) {
            asked = q;
            return true;
        },
        true));
    EXPECT_EQ(asked, question);
    const auto no = hio::cli::confirmDestructive(
        human, false, question, [](const std::string&) { return false; }, true);
    ASSERT_FALSE(no);
    EXPECT_EQ(no.error().code, ErrorCode::Cancelled);
}

TEST(FlashInput, ArgumentRules) {
    QTemporaryDir dir;
    const fs::path root(dir.path().toStdString());
    std::ofstream(root / "app.hex") << ":00000001FF\n";
    const auto file = hio::cli::resolveFlashInput((root / "app.hex").string(), root);
    ASSERT_TRUE(file);
    EXPECT_TRUE(std::holds_alternative<hio::cli::FlashFile>(*file));
    const auto id = hio::cli::resolveFlashInput("0123456789ABCDEF0123456789abcdef", root);
    ASSERT_TRUE(id);
    EXPECT_TRUE(std::holds_alternative<hio::cli::FlashCatalogId>(*id));
    const auto missing = hio::cli::resolveFlashInput((root / "nope.hex").string(), root);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::UsageInvalidArgument);
}

TEST(FlashInput, BuildOutputSearch) {
    QTemporaryDir dir;
    const fs::path root(dir.path().toStdString());
    const auto none = hio::cli::resolveFlashInput("", root);
    ASSERT_FALSE(none);
    EXPECT_NE(none.error().detail.find("merged_chester_nrf52840.hex"), std::string::npos);
    fs::create_directories(root / "build" / "zephyr");
    {
        const std::ofstream empty(root / "build" / "merged.hex"); // empty: skipped
    }
    std::ofstream(root / "build" / "zephyr" / "zephyr.hex") << ":00000001FF\n";
    const auto found = hio::cli::resolveFlashInput("", root);
    ASSERT_TRUE(found);
    EXPECT_EQ(std::get<hio::cli::FlashFile>(*found).path, root / "build" / "zephyr" / "zephyr.hex");
    std::ofstream(root / "build" / "zephyr.hex") << ":00000001FF\n"; // build/ comes first
    EXPECT_EQ(std::get<hio::cli::FlashFile>(*hio::cli::resolveFlashInput("", root)).path,
              root / "build" / "zephyr.hex");
}

} // namespace

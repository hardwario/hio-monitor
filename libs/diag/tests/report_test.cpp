// Redaction and the diagnostic report (plan/features/0007, A5; ADR-0075). All names are fake.

#include "hio/core/crash_handler.h"
#include "hio/core/error.h"
#include "hio/diag/check.h"
#include "hio/diag/report.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtSystemDetection>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <miniz.h>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::diag::CheckResult;
using hio::diag::CheckStatus;
using hio::diag::redact;
using hio::diag::RedactionContext;

const RedactionContext posix{
    .home = "/home/jdoe", .user = "jdoe", .host = "jdoe-box", .caseInsensitive = false};
const RedactionContext windows{
    .home = "C:\\Users\\JDoe", .user = "JDoe", .host = "JDOE-PC", .caseInsensitive = true};

TEST(Redact, HomeUserAndHost) {
    EXPECT_EQ(redact("log /home/jdoe/.local/share/x.log", posix), "log ~/.local/share/x.log");
    EXPECT_EQ(redact("user jdoe on jdoe-box", posix), "user <user> on <host>");
    EXPECT_EQ(redact("jdoe-box:/home/jdoe", posix), "<host>:~");
}

TEST(Redact, SubstringsInLongerWordsStay) {
    EXPECT_EQ(redact("jdoes jdoe_x xjdoe /home/jdoenet", posix),
              "jdoes jdoe_x xjdoe /home/jdoenet");
    EXPECT_EQ(redact("JDOE is not jdoe on POSIX", posix), "JDOE is not <user> on POSIX");
    EXPECT_EQ(redact("serial 801000001, fw abc", posix), "serial 801000001, fw abc");
}

TEST(Redact, WindowsIsCaseInsensitiveAndKnowsBothSeparators) {
    EXPECT_EQ(redact("c:\\users\\jdoe\\AppData\\x", windows), "~\\AppData\\x");
    EXPECT_EQ(redact("C:/Users/JDoe/AppData", windows), "~/AppData");
    EXPECT_EQ(redact("user jdoe host jdoe-pc", windows), "user <user> host <host>");
}

// Review V13: the home folder itself (followed by a separator, a quote, a space, ':' or the end)
// becomes "~"; a sibling folder is another folder. Its name follows the user-name rule: "jane" in
// "jane_old" is part of another name and stays; "jane.bak" contains the whole word "jane".
TEST(Redact, OnlyTheHomeFolderItselfIsTheHomePath) {
    const RedactionContext jane{
        .home = "/home/jane", .user = "jane", .host = "", .caseInsensitive = false};
    EXPECT_EQ(redact("/home/jane/x", jane), "~/x");
    EXPECT_EQ(redact("cd /home/jane", jane), "cd ~");
    EXPECT_EQ(redact("'/home/jane' and \"/home/jane\" or /home/jane: x", jane),
              "'~' and \"~\" or ~: x");
    EXPECT_EQ(redact("/home/jane_old/x", jane), "/home/jane_old/x");
    EXPECT_EQ(redact("/home/jane-old/x", jane), "/home/jane-old/x");
    EXPECT_EQ(redact("/home/jane.bak/x", jane), "/home/<user>.bak/x");
    EXPECT_EQ(redact("/home/janet/x", jane), "/home/janet/x");
    const RedactionContext windowsJane{
        .home = "C:\\Users\\Jane", .user = "Jane", .host = "", .caseInsensitive = true};
    EXPECT_EQ(redact("C:\\Users\\Jane\\x and C:\\Users\\Jane_old\\x", windowsJane),
              "~\\x and C:\\Users\\Jane_old\\x");
}

// Review V13: macOS logs often show the host name without ".local".
TEST(Redact, ShortHostNameIsReplacedToo) {
    const RedactionContext mac{
        .home = "/Users/jdoe", .user = "jdoe", .host = "Janes-MBP.local", .caseInsensitive = false};
    EXPECT_EQ(redact("on Janes-MBP.local and Janes-MBP, not Janes-MBPs", mac),
              "on <host> and <host>, not Janes-MBPs");
    // A short name of fewer than 3 characters would hit ordinary words: kept.
    const RedactionContext shortHost{
        .home = "/home/x1", .user = "x1", .host = "ab.example.org", .caseInsensitive = false};
    EXPECT_EQ(redact("ab.example.org ab", shortHost), "<host> ab");
    const RedactionContext threeChars{
        .home = "/home/x1", .user = "x1", .host = "pc1.lan", .caseInsensitive = false};
    EXPECT_EQ(redact("pc1.lan pc1 pc10", threeChars), "<host> <host> pc10");
}

TEST(Redact, ShortNamesAreNotReplaced) {
    const RedactionContext tiny{.home = "/", .user = "a", .host = "", .caseInsensitive = false};
    EXPECT_EQ(redact("a / b", tiny), "a / b");
}

TEST(Report, TailStartsAtALine) {
    EXPECT_EQ(hio::diag::tailAtLineStart("abc", 10), "abc");
    EXPECT_EQ(hio::diag::tailAtLineStart("line1\nline2\nline3\n", 9), "line3\n");
    EXPECT_EQ(hio::diag::tailAtLineStart("line1\nline2\nline3\n", 12), "line2\nline3\n");
    EXPECT_EQ(hio::diag::tailAtLineStart("xxxxxxxxxx", 4), "xxxx");
}

hio::diag::ReportInput sampleInput(const fs::path& dir) {
    hio::diag::ReportInput input;
    input.redaction = posix;
    input.createdUtc = "2026-10-16T10:00:00Z";
    input.app = QJsonObject{{"version", "2.0.0"},     {"gitHash", "abc1234"}, {"qt", "6.8.3"},
                            {"compiler", "Clang 21"}, {"os", "Test OS"},      {"kernel", "linux 6"},
                            {"arch", "x86_64"},       {"language", "en"}};
    input.tools = QJsonObject{
        {"jlink", QJsonObject{{"path", "/home/jdoe/SEGGER/libjlinkarm.so"}, {"version", "8.10"}}}};
    input.probes = QJsonArray{QJsonObject{{"serialNumber", 801000001},
                                          {"product", "Test Probe"},
                                          {"firmware", "fw"},
                                          {"firmwareStatus", "current"},
                                          {"inUseBy", ""}}};
    input.doctor = {
        CheckResult{.id = "jlink-software",
                    .status = CheckStatus::Ok,
                    .title = "J-Link Software",
                    .result = "8.10 \u00B7 /home/jdoe/SEGGER",
                    .error = std::nullopt},
        CheckResult{.id = "app-dirs",
                    .status = CheckStatus::Failed,
                    .title = "Settings and log folders",
                    .result = "not writable",
                    .error = hio::makeError(ErrorCode::IoWriteFailed, "/home/jdoe/.config")}};
    const fs::path logs = dir / "logs";
    fs::create_directories(logs);
    {
        std::ofstream big(logs / "gui-20261016-090000-1.log", std::ios::binary);
        const std::string line = "2026 [inf] hio.core user jdoe opened /home/jdoe/file\n";
        for (std::size_t written = 0; written < hio::diag::maxLogBytes + 4096;
             written += line.size()) {
            big << line;
        }
    }
    std::ofstream(logs / "cli-20261016-090100-2.log") << "cli on jdoe-box\n";
    input.logFiles = hio::diag::recentLogFiles(logs);
    std::ofstream(dir / "Monitor.ini") << "[jlink]\nlibraryPath=/home/jdoe/x.so\n";
    input.settingsFile = dir / "Monitor.ini";
    return input;
}

std::map<std::string, std::string> unzip(const fs::path& file) {
    QFile zip(QString::fromStdString(file.string()));
    EXPECT_TRUE(zip.open(QIODevice::ReadOnly));
    const QByteArray bytes = zip.readAll();
    mz_zip_archive archive{};
    mz_zip_zero_struct(&archive);
    std::map<std::string, std::string> entries;
    if (mz_zip_reader_init_mem(&archive, bytes.constData(), static_cast<std::size_t>(bytes.size()),
                               0) == MZ_FALSE) {
        return entries;
    }
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); ++i) {
        mz_zip_archive_file_stat stat{};
        mz_zip_reader_file_stat(&archive, i, &stat);
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
        entries[&stat.m_filename[0]] = std::string(static_cast<const char*>(data), size);
        mz_free(data);
    }
    mz_zip_reader_end(&archive);
    return entries;
}

TEST(Report, SummaryIsRedacted) {
    const QTemporaryDir dir;
    const auto input = sampleInput(fs::path(dir.path().toStdString()));
    const std::string summary = hio::diag::ReportBuilder::summaryText(input);
    EXPECT_NE(summary.find("HARDWARIO Monitor diagnostic report"), std::string::npos);
    EXPECT_NE(summary.find("J-Link Software: 8.10 \u00B7 ~/SEGGER/libjlinkarm.so"),
              std::string::npos);
    EXPECT_NE(summary.find("801000001 Test Probe"), std::string::npos); // serials stay
    EXPECT_NE(summary.find("\u2717 Settings and log folders"), std::string::npos);
    EXPECT_NE(summary.find("io.write_failed"), std::string::npos);
    EXPECT_EQ(summary.find("jdoe"), std::string::npos) << summary;
    const QJsonObject json = hio::diag::ReportBuilder::summaryJson(input);
    EXPECT_EQ(json.value("checks").toArray().size(), 2);
    EXPECT_EQ(json.value("checks").toArray().at(1).toObject().value("status").toString(), "failed");
    EXPECT_EQ(QString::fromUtf8(QJsonDocument(json).toJson()).indexOf("jdoe"), -1);
}

TEST(Report, ZipHasTheEntriesWithinBounds) {
    const QTemporaryDir dir;
    const fs::path root(dir.path().toStdString());
    const auto input = sampleInput(root);
    ASSERT_EQ(input.logFiles.size(), 2U);
    const fs::path target = root / "report.zip";
    const auto bytes = hio::diag::ReportBuilder::writeZip(input, target, {});
    ASSERT_TRUE(bytes.has_value()) << bytes.error().detail;
    EXPECT_EQ(*bytes, fs::file_size(target));
    EXPECT_LE(*bytes, hio::diag::maxReportBytes);
    const auto entries = unzip(target);
    ASSERT_TRUE(entries.contains("summary.txt"));
    ASSERT_TRUE(entries.contains("summary.json"));
    ASSERT_TRUE(entries.contains("settings.ini"));
    ASSERT_TRUE(entries.contains("logs/gui-20261016-090000-1.log"));
    ASSERT_TRUE(entries.contains("logs/cli-20261016-090100-2.log"));
    const std::string& log = entries.at("logs/gui-20261016-090000-1.log");
    EXPECT_LE(log.size(), hio::diag::maxLogBytes);
    EXPECT_TRUE(log.starts_with("2026 [inf]")) << log.substr(0, 40);
    for (const auto& [name, data] : entries) {
        EXPECT_EQ(data.find("jdoe"), std::string::npos) << name;
    }
    EXPECT_EQ(entries.at("settings.ini"), "[jlink]\nlibraryPath=~/x.so\n");
}

TEST(Report, FailedWriteLeavesNoFile) {
    const QTemporaryDir dir;
    const fs::path root(dir.path().toStdString());
    const auto input = sampleInput(root);
    const fs::path target = root / "missing-folder" / "report.zip";
    const auto bytes = hio::diag::ReportBuilder::writeZip(input, target, {});
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().code, ErrorCode::DiagReportWriteFailed);
    EXPECT_FALSE(fs::exists(target));
    EXPECT_FALSE(fs::exists(root / "missing-folder"));
}

TEST(Report, CancelledWriteLeavesNoFile) {
    const QTemporaryDir dir;
    const fs::path root(dir.path().toStdString());
    const auto input = sampleInput(root);
    std::stop_source stop;
    stop.request_stop();
    const auto bytes = hio::diag::ReportBuilder::writeZip(input, root / "r.zip", stop.get_token());
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(fs::exists(root / "r.zip"));
    for (const auto& entry : fs::directory_iterator(root)) {
        EXPECT_FALSE(entry.path().filename().string().starts_with("r.zip")) << entry.path();
    }
}

// ADR-0123 (plan 0015 B2): the newest crash files go into crashes/, text redacted, dumps as
// they are; a file over the remaining budget is skipped and listed as skipped in summary.txt.
TEST(Report, CrashFilesAreIncludedWithinTheBound) {
    const QTemporaryDir dir;
    const fs::path root(dir.path().toStdString());
    auto input = sampleInput(root);
    const fs::path crashes = root / "crashes";
    fs::create_directories(crashes);
    std::ofstream(crashes / "crash-hio-monitor-20261016-100000.txt")
        << "HARDWARIO Monitor crash report\n/home/jdoe/build/hio-monitor(+0x10)\n";
    {
        std::ofstream dump(crashes / "crash-hio-20261016-090000.dmp", std::ios::binary);
        dump << std::string(1024, '\x01');
    }
    // Two dumps of the maximum size: the second does not fit into what is left (skipped).
    for (const char* name : {"crash-hio-20261015-090000.dmp", "crash-hio-20261014-090000.dmp"}) {
        std::ofstream dump(crashes / name, std::ios::binary);
        dump << std::string(hio::crash::maxDumpBytes, '\x02');
    }
    input.crashFiles = {crashes / "crash-hio-monitor-20261016-100000.txt",
                        crashes / "crash-hio-20261016-090000.dmp",
                        crashes / "crash-hio-20261015-090000.dmp",
                        crashes / "crash-hio-20261014-090000.dmp"};
    const fs::path target = root / "report.zip";
    const auto bytes = hio::diag::ReportBuilder::writeZip(input, target, {});
    ASSERT_TRUE(bytes.has_value()) << bytes.error().detail;
    EXPECT_LE(*bytes, hio::diag::maxReportBytes);
    const auto entries = unzip(target);
    ASSERT_TRUE(entries.contains("crashes/crash-hio-monitor-20261016-100000.txt"));
    EXPECT_EQ(entries.at("crashes/crash-hio-monitor-20261016-100000.txt"),
              "HARDWARIO Monitor crash report\n~/build/hio-monitor(+0x10)\n");
    ASSERT_TRUE(entries.contains("crashes/crash-hio-20261016-090000.dmp"));
    EXPECT_EQ(entries.at("crashes/crash-hio-20261016-090000.dmp"), std::string(1024, '\x01'));
    EXPECT_TRUE(entries.contains("crashes/crash-hio-20261015-090000.dmp"));
    EXPECT_FALSE(entries.contains("crashes/crash-hio-20261014-090000.dmp"));
    const std::string& summary = entries.at("summary.txt");
    EXPECT_NE(summary.find("Crash files: 4"), std::string::npos) << summary;
    EXPECT_NE(summary.find("crash-hio-20261014-090000.dmp (skipped: the report size limit)"),
              std::string::npos)
        << summary;
    EXPECT_NE(summary.find("  crash-hio-20261016-090000.dmp\n"), std::string::npos) << summary;
}

TEST(Report, RecentCrashFilesAreTheNewestThree) {
    const QTemporaryDir dir;
    const fs::path crashes(dir.path().toStdString());
    const auto now = fs::file_time_type::clock::now();
    for (int i = 0; i < 5; ++i) {
        const fs::path file = crashes / ("crash-hio-2026101" + std::to_string(i) + "-000000.txt");
        std::ofstream(file) << i << '\n';
        fs::last_write_time(file, now - std::chrono::hours(10 - i));
    }
    std::ofstream(crashes / "last-crash") << "x\n";
    const auto files = hio::diag::recentCrashFiles(crashes);
#ifdef Q_OS_MACOS
    ASSERT_GE(files.size(), 3U); // plus the user's own DiagnosticReports/hio* files, if any
#else
    ASSERT_EQ(files.size(), 3U);
#endif
    EXPECT_EQ(files.at(0).filename(), "crash-hio-20261014-000000.txt");
    EXPECT_EQ(files.at(2).filename(), "crash-hio-20261012-000000.txt");
}

TEST(Report, SummaryWithoutCrashFiles) {
    const QTemporaryDir dir;
    const auto input = sampleInput(fs::path(dir.path().toStdString()));
    EXPECT_NE(hio::diag::ReportBuilder::summaryText(input).find("Crash files: none"),
              std::string::npos);
}

TEST(Report, RecentLogsAreTheTwoNewestRunsPerKind) {
    const QTemporaryDir dir;
    const fs::path logs(dir.path().toStdString());
    for (const char* name :
         {"gui-20261014-100000-1.log", "gui-20261015-100000-2.log", "gui-20261016-100000-3.log",
          "gui-20261016-100000-3.part2.log", "cli-20261016-100000-4.log", "other.log",
          "cli-20261016-100000-4.txt"}) {
        std::ofstream(logs / name) << "x\n";
    }
    const auto files = hio::diag::recentLogFiles(logs);
    std::vector<std::string> names;
    names.reserve(files.size());
    for (const auto& file : files) {
        names.push_back(file.filename().string());
    }
    ASSERT_EQ(names.size(), 3U);
    EXPECT_EQ(names[0], "gui-20261015-100000-2.log");
    EXPECT_TRUE(names[1].starts_with("gui-20261016-100000-3"));
    EXPECT_EQ(names[2], "cli-20261016-100000-4.log");
}

TEST(Report, DefaultFileName) {
    const std::string name = hio::diag::defaultReportFileName();
    EXPECT_TRUE(name.starts_with("hio-diagnostics-"));
    EXPECT_TRUE(name.ends_with(".zip"));
    EXPECT_EQ(name.size(), std::string("hio-diagnostics-20261016-101010.zip").size());
}

} // namespace

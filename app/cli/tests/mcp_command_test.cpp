// Unit tests of the `hwio mcp` parts (plan/features/0013 C6): --allow parsing, FlagConsentBroker,
// the StdinLineSource adapter (run under TSan too) and CliDeviceProvider's id rules.

#include "cli_device_provider.h"
#include "flag_consent_broker.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/core/settings.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/jlink/library_locator.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/offload.h"
#include "hwio/mcp/stdio_transport.h"
#include "hwio/mcp/tools/device_provider.h"
#include "mcp_command.h"
#include "stdin_line_source.h"
#include "terminal.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using hwio::cli::CliDeviceProvider;
using hwio::cli::FlagConsentBroker;
using hwio::cli::parseAllowList;
using hwio::cli::StdinLineReader;
using hwio::cli::StdinLineSource;
using hwio::mcp::ConsentCategories;
using hwio::mcp::ConsentCategory;
using ReadStatus = StdinLineReader::ReadStatus;

TEST(McpAllowList, ParsesItemsAndAll) {
    EXPECT_EQ(parseAllowList("").value(), ConsentCategories());
    EXPECT_EQ(parseAllowList("flash").value(), ConsentCategories(ConsentCategory::Flash));
    EXPECT_EQ(parseAllowList(" flash , memory-write,,").value(),
              ConsentCategories(ConsentCategory::Flash) | ConsentCategory::MemoryWrite);
    EXPECT_EQ(parseAllowList("all").value(), hwio::cli::allDestructiveCategories());
    EXPECT_FALSE(parseAllowList("all").value().testFlag(ConsentCategory::Connect));
}

TEST(McpAllowList, UnknownItemIsAUsageError) {
    for (const char* list : {"flash,format", "connect", "FLASH", "memory_write"}) {
        const auto parsed = parseAllowList(list);
        ASSERT_FALSE(parsed) << list;
        EXPECT_EQ(parsed.error().code, hwio::ErrorCode::UsageInvalidArgument);
    }
}

hwio::mcp::ConsentDecision decide(FlagConsentBroker& broker, ConsentCategories categories,
                                  const char* tool = "flash") {
    std::optional<hwio::mcp::ConsentDecision> decision;
    hwio::mcp::ConsentRequest request;
    request.tool = tool;
    request.categories = categories;
    const auto cancel = broker.request(std::move(request),
                                       [&decision](auto answer) { decision = std::move(answer); });
    EXPECT_FALSE(cancel);
    return decision.value_or(hwio::mcp::ConsentDecision{});
}

TEST(FlagConsentBroker, DecidesFromTheFlags) {
    FlagConsentBroker broker(ConsentCategory::Flash);
    EXPECT_TRUE(decide(broker, ConsentCategory::Flash));
    EXPECT_TRUE(decide(broker, ConsentCategory::Connect)); // always allowed in the CLI
    const auto erase = decide(broker, ConsentCategory::Erase, "erase");
    ASSERT_FALSE(erase);
    EXPECT_EQ(erase.error().code, hwio::ErrorCode::McpToolNotAllowed);
    EXPECT_EQ(erase.error().message, "The tool erase is not allowed by this server.");
    EXPECT_EQ(erase.error().suggestion, "Start the server with --allow erase (or --allow all).");
    EXPECT_EQ(erase.error().cause, "Destructive tools must be allowed when the server starts.");

    const auto uicr =
        decide(broker, ConsentCategories(ConsentCategory::Flash) | ConsentCategory::Uicr);
    ASSERT_FALSE(uicr);
    EXPECT_EQ(uicr.error().suggestion, "Start the server with --allow uicr (or --allow all).");

    FlagConsentBroker none{ConsentCategories()};
    const auto both = decide(none, ConsentCategories(ConsentCategory::Flash) |
                                       ConsentCategory::Uicr | ConsentCategory::Connect);
    ASSERT_FALSE(both);
    EXPECT_EQ(both.error().suggestion,
              "Start the server with --allow flash,uicr (or --allow all).");

    // ADR-0161: erase scope=all / recover need erase and uicr; the suggestion names both.
    const auto wipe =
        decide(none, ConsentCategories(ConsentCategory::Erase) | ConsentCategory::Uicr, "recover");
    ASSERT_FALSE(wipe);
    EXPECT_EQ(wipe.error().message, "The tool recover is not allowed by this server.");
    EXPECT_EQ(wipe.error().suggestion,
              "Start the server with --allow erase,uicr (or --allow all).");

    // Review R13: a tool name with "%1" is shown as it is.
    const auto odd = decide(none, ConsentCategory::Flash, "x%1y");
    ASSERT_FALSE(odd);
    EXPECT_EQ(odd.error().message, "The tool x%1y is not allowed by this server.");

    FlagConsentBroker all(hwio::cli::allDestructiveCategories());
    EXPECT_TRUE(decide(all, ConsentCategories(ConsentCategory::Flash) | ConsentCategory::Erase |
                                ConsentCategory::MemoryWrite | ConsentCategory::Uicr));
}

// Delivers `parts` one per read call, then the end of input.
StdinLineReader::ReadFunction chunks(std::vector<std::string> parts) {
    return [parts = std::move(parts), index = std::size_t{0}](std::span<char> buffer,
                                                              std::size_t& count) mutable {
        if (index >= parts.size()) {
            count = 0;
            return ReadStatus::End;
        }
        const std::string& part = parts[index++];
        part.copy(buffer.data(), part.size());
        count = part.size();
        return ReadStatus::Data;
    };
}

TEST(StdinLineSource, DeliversLinesOverlongMarkersAndTheEnd) {
    std::atomic<int> wakeups{0};
    StdinLineSource source(8, 4, 0, chunks({"one\n", "much too long line\n", "two\r\n"}));
    source.setWakeup([&wakeups] { ++wakeups; });
    std::vector<hwio::mcp::ILineSource::Line> lines;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!source.atEnd() && std::chrono::steady_clock::now() < deadline) {
        if (auto line = source.takeLine()) {
            lines.push_back(std::move(*line));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ASSERT_TRUE(source.atEnd());
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0].text, QByteArray("one"));
    EXPECT_FALSE(lines[0].overlong);
    EXPECT_TRUE(lines[1].overlong);
    EXPECT_EQ(lines[2].text, QByteArray("two"));
    EXPECT_FALSE(source.takeLine().has_value()); // nothing after the end
    EXPECT_GE(wakeups.load(), 1);
}

TEST(StdinLineSource, ReplacedWakeupIsNeverCalledAgain) {
    std::atomic<bool> stop{false};
    std::atomic<int> oldCalls{0};
    // An endless stream of short lines; the queue fills and the reader waits (backpressure).
    StdinLineSource source(64, 2, 0, [&stop](std::span<char> buffer, std::size_t& count) {
        if (stop.load()) {
            count = 0;
            return ReadStatus::End;
        }
        const std::string line = "x\n";
        line.copy(buffer.data(), line.size());
        count = line.size();
        return ReadStatus::Data;
    });
    source.setWakeup([&oldCalls] { ++oldCalls; });
    for (int i = 0; i < 50; ++i) {
        (void)source.takeLine();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    source.setWakeup({});
    const int after = oldCalls.load();
    for (int i = 0; i < 50; ++i) {
        (void)source.takeLine();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(oldCalls.load(), after);
    stop = true;
}

// ADR-0163: mcp/stdioBufferMiB, default 16, allowed 4-256, invalid -> default, out of range ->
// clamped, both with a warning.
TEST(McpStdioBuffer, SettingIsReadAndClamped) {
    EXPECT_EQ(hwio::cli::stdioBufferSetting(nullptr).mebibytes, 16);
    EXPECT_FALSE(hwio::cli::stdioBufferSetting(nullptr).warning);

    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path file =
        std::filesystem::path(dir.path().toStdU16String()) / "Monitor.ini";
    auto settings = hwio::Settings::openFile(file);
    ASSERT_TRUE(settings);
    const hwio::SettingKey<std::string> key{.path = hwio::cli::stdioBufferKey, .defaultValue = {}};
    EXPECT_EQ(hwio::cli::stdioBufferSetting(&*settings).mebibytes, 16); // not set
    EXPECT_FALSE(hwio::cli::stdioBufferSetting(&*settings).warning);

    struct Case {
        const char* stored;
        int expected;
        bool warning;
    };
    const std::array cases{Case{.stored = "32", .expected = 32, .warning = false},
                           Case{.stored = "4", .expected = 4, .warning = false},
                           Case{.stored = "256", .expected = 256, .warning = false},
                           Case{.stored = "1", .expected = 4, .warning = true},
                           Case{.stored = "0", .expected = 4, .warning = true},
                           Case{.stored = "-5", .expected = 4, .warning = true},
                           Case{.stored = "1000", .expected = 256, .warning = true},
                           Case{.stored = "abc", .expected = 16, .warning = true},
                           Case{.stored = "12.5", .expected = 16, .warning = true},
                           Case{.stored = " 64 ", .expected = 64, .warning = false},
                           Case{.stored = "99999999999", .expected = 256, .warning = true}};
    for (const auto& item : cases) {
        ASSERT_TRUE(settings->set(key, std::string(item.stored)));
        const hwio::cli::StdioBufferSetting setting = hwio::cli::stdioBufferSetting(&*settings);
        EXPECT_EQ(setting.mebibytes, item.expected) << item.stored;
        EXPECT_EQ(setting.warning.has_value(), item.warning) << item.stored;
    }
    ASSERT_TRUE(settings->set(key, std::string("1000")));
    EXPECT_EQ(hwio::cli::stdioBufferSetting(&*settings).warning.value_or(std::string()),
              "The setting mcp/stdioBufferMiB = 1000 is outside 4-256; using 256 MiB.");
}

// ADR-0162: list() never enumerates; refresh() without an offloader fails at once.
TEST(CliDeviceProvider, RefreshNeedsAnOffloader) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdU16String());
    int located = 0;
    CliDeviceProvider provider(
        [&located]() -> hwio::Result<hwio::jlink::LibraryLocation> {
            ++located;
            return hwio::jlink::LibraryLocation{.path = HWIO_FAKE_JLINK_LIBRARY,
                                                .versionFromName = 0,
                                                .source = hwio::jlink::LibrarySource::Option};
        },
        hwio::cli::CliProviderOptions{},
        hwio::device::RegistryOptions{.instanceRoot = root / "instances",
                                      .lockDirectory = root / "locks",
                                      .consoleDirectory = root / "console"});
    EXPECT_TRUE(provider.list().empty());
    EXPECT_EQ(located, 0); // list() does not even locate the library
    std::optional<hwio::Status> outcome;
    provider.refresh([&outcome](hwio::Status status) { outcome = std::move(status); });
    ASSERT_TRUE(outcome.has_value());
    const hwio::Status refreshed = outcome.value_or(hwio::Status{});
    ASSERT_FALSE(refreshed);
    EXPECT_EQ(refreshed.error().code, hwio::ErrorCode::InternalUnexpected);
}

// ADR-0162 (TSan): the enumeration runs on the offloader; concurrent refreshes join the running
// one; the cache is filled on the owner thread before `done`.
TEST(CliDeviceProvider, RefreshIsOffloaded) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdU16String());
    {
        QFile scenario(dir.filePath(QStringLiteral("scenario.json")));
        ASSERT_TRUE(scenario.open(QIODevice::WriteOnly));
        scenario.write(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe"}]})");
    }
    qputenv("HWIO_FAKE_JLINK_SCENARIO", dir.filePath(QStringLiteral("scenario.json")).toUtf8());
    CliDeviceProvider provider(
        []() -> hwio::Result<hwio::jlink::LibraryLocation> {
            return hwio::jlink::LibraryLocation{.path = HWIO_FAKE_JLINK_LIBRARY,
                                                .versionFromName = 0,
                                                .source = hwio::jlink::LibrarySource::Option};
        },
        hwio::cli::CliProviderOptions{},
        hwio::device::RegistryOptions{.instanceRoot = root / "instances",
                                      .lockDirectory = root / "locks",
                                      .consoleDirectory = root / "console"});
    hwio::mcp::Offloader offloader(4, 16);
    provider.setOffloader(&offloader);
    int changed = 0;
    QObject::connect(&provider.notifier(),
                     &hwio::mcp::tools::DeviceProviderNotifier::devicesChanged,
                     [&changed] { ++changed; });
    std::vector<hwio::Status> outcomes;
    std::vector<std::size_t> sizes;
    for (int i = 0; i < 2; ++i) {
        provider.refresh([&](hwio::Status status) {
            outcomes.push_back(std::move(status));
            sizes.push_back(provider.list().size());
        });
    }
    EXPECT_TRUE(outcomes.empty());      // asynchronous
    EXPECT_EQ(offloader.running(), 1U); // one job for both refreshes
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (outcomes.size() < 2 && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    qunsetenv("HWIO_FAKE_JLINK_SCENARIO");
    ASSERT_EQ(outcomes.size(), 2U);
    EXPECT_TRUE(outcomes[0]) << outcomes[0].error().detail;
    EXPECT_TRUE(outcomes[1]);
    EXPECT_EQ(sizes, (std::vector<std::size_t>{1, 1}));
    EXPECT_EQ(changed, 1);
    const auto entries = provider.list();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].id, "jlink:801000001");
    EXPECT_EQ(entries[0].state, hwio::mcp::tools::DeviceEntry::State::Available);
}

// A provider over the fake J-Link library with one probe (801000001) in `dir`.
class FakeProbeProvider {
public:
    explicit FakeProbeProvider(const QTemporaryDir& dir)
        : m_root(dir.path().toStdU16String()),
          m_provider(
              []() -> hwio::Result<hwio::jlink::LibraryLocation> {
                  return hwio::jlink::LibraryLocation{.path = HWIO_FAKE_JLINK_LIBRARY,
                                                      .versionFromName = 0,
                                                      .source = hwio::jlink::LibrarySource::Option};
              },
              hwio::cli::CliProviderOptions{},
              hwio::device::RegistryOptions{.instanceRoot = m_root / "instances",
                                            .lockDirectory = m_root / "locks",
                                            .consoleDirectory = m_root / "console"}) {
        QFile scenario(dir.filePath(QStringLiteral("scenario.json")));
        if (scenario.open(QIODevice::WriteOnly)) {
            scenario.write(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe"}]})");
        }
        qputenv("HWIO_FAKE_JLINK_SCENARIO", dir.filePath(QStringLiteral("scenario.json")).toUtf8());
    }
    ~FakeProbeProvider() { qunsetenv("HWIO_FAKE_JLINK_SCENARIO"); }
    FakeProbeProvider(const FakeProbeProvider&) = delete;
    FakeProbeProvider& operator=(const FakeProbeProvider&) = delete;
    FakeProbeProvider(FakeProbeProvider&&) = delete;
    FakeProbeProvider& operator=(FakeProbeProvider&&) = delete;

    [[nodiscard]] CliDeviceProvider& get() { return m_provider; }

private:
    std::filesystem::path m_root;
    CliDeviceProvider m_provider;
};

// Runs the event loop until `done` holds (at most 20 s).
template <class Predicate> bool processUntil(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

// Review V1: the first refresh (the start-up enumeration) is pending until it has finished; a
// refresh meanwhile joins it (one job); later refreshes are not "first" any more.
TEST(CliDeviceProvider, FirstScanIsPendingUntilItHasFinished) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    FakeProbeProvider fake(dir);
    CliDeviceProvider& provider = fake.get();
    hwio::mcp::Offloader offloader(4, 16);
    provider.setOffloader(&offloader);
    EXPECT_FALSE(provider.firstScanPending());
    std::vector<hwio::Status> outcomes;
    provider.refresh([&](hwio::Status status) { outcomes.push_back(std::move(status)); });
    EXPECT_TRUE(provider.firstScanPending());
    provider.refresh([&](hwio::Status status) { outcomes.push_back(std::move(status)); });
    EXPECT_EQ(offloader.running(), 1U); // joined: no second enumeration
    ASSERT_TRUE(processUntil([&] { return outcomes.size() == 2; }));
    EXPECT_TRUE(outcomes[0]);
    EXPECT_FALSE(provider.firstScanPending());
    EXPECT_EQ(provider.list().size(), 1U);
    provider.refresh([&](hwio::Status status) { outcomes.push_back(std::move(status)); });
    EXPECT_FALSE(provider.firstScanPending()); // a later refresh is not the first scan
    ASSERT_TRUE(processUntil([&] { return outcomes.size() == 3; }));
}

// Review F3: a dropped enumeration job (stopAll, or the offloader destroyed) fails every waiting
// refresh once with `cancelled`, from a later event-loop pass; the first scan is then over.
TEST(CliDeviceProvider, DroppedRefreshFailsTheWaitersWithCancelled) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    FakeProbeProvider fake(dir);
    CliDeviceProvider& provider = fake.get();
    for (const bool destroy : {false, true}) {
        auto offloader = std::make_unique<hwio::mcp::Offloader>(4, 16);
        provider.setOffloader(offloader.get());
        std::vector<hwio::Status> outcomes;
        for (int i = 0; i < 2; ++i) {
            provider.refresh([&](hwio::Status status) { outcomes.push_back(std::move(status)); });
        }
        if (destroy) {
            offloader.reset();
        } else {
            offloader->stopAll();
        }
        EXPECT_TRUE(outcomes.empty()) << destroy; // never inside the Offloader's loops
        ASSERT_TRUE(processUntil([&] { return outcomes.size() == 2; })) << destroy;
        for (const hwio::Status& outcome : outcomes) {
            ASSERT_FALSE(outcome) << destroy;
            EXPECT_EQ(outcome.error().code, hwio::ErrorCode::Cancelled) << destroy;
        }
        EXPECT_FALSE(provider.firstScanPending()) << destroy;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        EXPECT_EQ(outcomes.size(), 2U) << destroy; // exactly once
    }
    // The provider still works afterwards.
    hwio::mcp::Offloader offloader(4, 16);
    provider.setOffloader(&offloader);
    std::optional<hwio::Status> outcome;
    provider.refresh([&](hwio::Status status) { outcome = std::move(status); });
    ASSERT_TRUE(processUntil([&] { return outcome.has_value(); }));
    EXPECT_TRUE(outcome.value_or(hwio::Status{}));
}

TEST(CliDeviceProvider, ProbeIds) {
    EXPECT_EQ(CliDeviceProvider::probeSerialOf("jlink:801000001"), 801000001U);
    EXPECT_EQ(CliDeviceProvider::probeSerialOf("jlink:4294967295"), 4294967295U);
    EXPECT_FALSE(CliDeviceProvider::probeSerialOf("jlink:4294967296"));
    EXPECT_FALSE(CliDeviceProvider::probeSerialOf("jlink:0801000001"));
    EXPECT_FALSE(CliDeviceProvider::probeSerialOf("jlink:"));
    EXPECT_FALSE(CliDeviceProvider::probeSerialOf("jlink:12a"));
    EXPECT_FALSE(CliDeviceProvider::probeSerialOf("ble:AA:BB"));
    EXPECT_FALSE(CliDeviceProvider::probeSerialOf("801000001"));
}

} // namespace

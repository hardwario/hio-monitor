#pragma once

// GMock of IBleApi (ADR-0087) and a recording ILinkHandler, shared with the libs/device tests.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/link_handler.h"
#include "hio/hw/ble/types.h"

#include <QCoreApplication>
#include <QEventLoop>

#include <gmock/gmock.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hio::ble::test {

class MockBleApi : public IBleApi {
public:
    MOCK_METHOD(void, setListener, (IBleListener*), (override));
    MOCK_METHOD(AdapterInfo, adapter, (), (override));
    MOCK_METHOD(Status, startScan, (std::chrono::milliseconds), (override));
    MOCK_METHOD(void, stopScan, (), (override));
    MOCK_METHOD(Result<LinkId>, connect, (const BleDeviceId&, const NusUuids&), (override));
    MOCK_METHOD(Status, write, (LinkId, std::span<const std::byte>, WriteMode, std::uint64_t),
                (override));
    MOCK_METHOD(void, disconnect, (LinkId), (override));
    MOCK_METHOD(Status, requestPairing, (const BleDeviceId&), (override));
    MOCK_METHOD(bool, isPaired, (const BleDeviceId&), (override));
};

inline AdapterInfo readyAdapter() {
    return AdapterInfo{
        .state = AdapterState::Ready, .name = "hci0", .address = "00:11:22:33:44:55"};
}

// What a link handler was told, in order (BLE thread; tests read it on the same thread).
struct HandlerLog {
    std::vector<std::string> events;  // "connecting", "prompt", "ready 247", "down <code>", ...
    std::vector<std::string> written; // lines
    std::vector<std::string> rejected;
    std::string data;
    int flushes = 0;
    bool closed = false;
};

class RecordingHandler : public ILinkHandler {
public:
    explicit RecordingHandler(std::shared_ptr<HandlerLog> log) : m_log(std::move(log)) {}
    void onConnecting(bool automatic) override {
        m_log->events.emplace_back(automatic ? "connecting auto" : "connecting");
    }
    void onPairingPrompt() override { m_log->events.emplace_back("prompt"); }
    void onReady(LinkReady ready) override {
        m_log->events.push_back("ready " + std::to_string(ready.mtu));
    }
    void onData(std::span<const std::byte> data) override {
        for (const std::byte b : data) {
            m_log->data.push_back(static_cast<char>(b));
        }
    }
    void onLineWritten(std::string line) override { m_log->written.push_back(std::move(line)); }
    void onLineRejected(std::string line, Error error) override {
        m_log->rejected.push_back(std::string(idOf(error.code)) + " " + line);
    }
    void onDown(Error error) override {
        m_log->events.push_back("down " + std::string(idOf(error.code)));
    }
    void onPairingFinished(Status status, bool alreadyPaired) override {
        m_log->events.push_back(std::string("paired ") + (status ? "ok" : "fail") +
                                (alreadyPaired ? " already" : ""));
    }
    void onFlush() override { ++m_log->flushes; }
    void onClosed() override { m_log->closed = true; }

private:
    std::shared_ptr<HandlerLog> m_log;
};

// Processes events until `done` or `timeout`.
inline bool runUntil(const std::function<bool()>& done,
                     std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

inline void runFor(std::chrono::milliseconds duration) {
    static_cast<void>(runUntil([] { return false; }, duration));
}

} // namespace hio::ble::test

#pragma once

// GMock of IJLinkApi (plan/features/0002-m1-jlink-rtt.md: vendor interfaces are mocked in unit
// tests, ADR-0010). Shared with the libs/device tests.

#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/types.h"

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::jlink::test {

class MockJLinkApi : public IJLinkApi {
public:
    MOCK_METHOD(std::uint32_t, libraryVersion, (), (override));
    MOCK_METHOD(std::string, compileDateTime, (), (override));
    MOCK_METHOD((std::expected<std::vector<ProbeInfo>, int>), listProbes, (ProbeHosts), (override));
    MOCK_METHOD(int, selectBySerialNumber, (std::uint32_t), (override));
    MOCK_METHOD(std::optional<std::string>, open, (), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(bool, isOpen, (), (override));
    MOCK_METHOD(std::string, execCommand, (std::string_view), (override));
    MOCK_METHOD(int, selectInterface, (TargetInterface), (override));
    MOCK_METHOD(void, setSpeed, (std::uint32_t), (override));
    MOCK_METHOD(int, connect, (), (override));
    MOCK_METHOD(bool, isConnected, (), (override));
    MOCK_METHOD(std::optional<HardwareStatus>, hardwareStatus, (), (override));
    MOCK_METHOD(std::string, firmwareString, (), (override));
    MOCK_METHOD((std::expected<std::string, int>), embeddedFirmwareString, (std::string_view),
                (override));
    MOCK_METHOD(std::uint32_t, updateFirmwareIfNewer, (), (override));
    MOCK_METHOD(std::uint32_t, serialNumber, (), (override));
    MOCK_METHOD(void, setResetDelay, (int), (override));
    MOCK_METHOD(int, reset, (), (override));
    MOCK_METHOD(int, halt, (), (override));
    MOCK_METHOD(void, go, (), (override));
    MOCK_METHOD(int, rttStart, (), (override));
    MOCK_METHOD(int, rttStop, (), (override));
    MOCK_METHOD(int, rttBufferCount, (RttDirection), (override));
    MOCK_METHOD((std::expected<RttBufferInfo, int>), rttBufferInfo, (int, RttDirection),
                (override));
    MOCK_METHOD(int, rttRead, (int, std::span<std::byte>), (override));
    MOCK_METHOD(int, rttWrite, (int, std::span<const std::byte>), (override));
    MOCK_METHOD(void, beginDownload, (), (override));
    MOCK_METHOD(int, writeMemory, (std::uint32_t, std::span<const std::byte>), (override));
    MOCK_METHOD(int, endDownload, (const FlashProgressFn&), (override));
    MOCK_METHOD(int, readMemory, (std::uint32_t, std::span<std::byte>), (override));
    MOCK_METHOD((std::expected<std::uint32_t, int>), readU32, (std::uint32_t), (override));
    MOCK_METHOD(int, eraseChip, (), (override));
    MOCK_METHOD(int, installUnsecureDialogHook, (), (override));
    MOCK_METHOD(int, coresightConfigure, (), (override));
    MOCK_METHOD((std::expected<std::uint32_t, int>), coresightRead, (std::uint8_t, bool),
                (override));
    MOCK_METHOD(int, coresightWrite, (std::uint8_t, bool, std::uint32_t), (override));
    MOCK_METHOD(void, flushMessages, (), (override));
    MOCK_METHOD(std::string, takeErrorText, (), (override));
};

} // namespace hio::jlink::test

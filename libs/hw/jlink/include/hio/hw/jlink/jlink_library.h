#pragma once

// JLinkLibrary - the only object that calls the vendor JLinkARM library (ADR-0006, ADR-0028).
//
// loadCopy() copies the located library into a fresh temporary directory under `instanceRoot`
// (ADR-0030: one copy per session, so several probes work in one process), loads the copy,
// resolves every function of jlink_abi.h, checks the version (>= 8.00, ADR-0029) and installs the
// log / warning / error out handlers. The destructor closes the probe if open, removes the
// handlers, unloads the copy and deletes its directory.
//
// Thread safety: none. The object belongs to one thread (the session worker or the caller of a
// short-lived enumeration); all vendor calls of this copy happen there.

#include "hio/core/result.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/library_locator.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::jlink {

class JLinkLibrary final : public IJLinkApi {
    struct Passkey {
        explicit Passkey() = default;
    };

public:
    struct Impl;

    // Errors: dependency.jlink_not_found (file missing), dependency.jlink_load_failed (copy or
    // load failed, missing symbol - named in `detail`), dependency.jlink_too_old,
    // internal.session_limit (no free out-handler slot).
    [[nodiscard]] static Result<std::unique_ptr<JLinkLibrary>>
    loadCopy(const LibraryLocation& location, const std::filesystem::path& instanceRoot);

    // AppLocalDataLocation/jlink-instances (requires initApplicationIdentity()).
    [[nodiscard]] static std::filesystem::path defaultInstanceRoot();

    // Removes instance directories under `root` older than `maxAge` (left behind by crashes)
    // whose lock file `instance.lock` can be taken (the owning process is gone) or that have
    // none; a copy in use by a live process is kept (review R14). Deletion failures are ignored.
    // Returns the number of removed directories.
    static std::size_t cleanupStaleInstances(const std::filesystem::path& root,
                                             std::chrono::hours maxAge = std::chrono::hours(24));

    JLinkLibrary(Passkey key, std::unique_ptr<Impl> impl);
    ~JLinkLibrary() override;
    JLinkLibrary(const JLinkLibrary&) = delete;
    JLinkLibrary& operator=(const JLinkLibrary&) = delete;
    JLinkLibrary(JLinkLibrary&&) = delete;
    JLinkLibrary& operator=(JLinkLibrary&&) = delete;

    // The located (original) library and the loaded copy.
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept;
    [[nodiscard]] const std::filesystem::path& copyPath() const noexcept;

    // IJLinkApi
    [[nodiscard]] std::uint32_t libraryVersion() override;
    [[nodiscard]] std::string compileDateTime() override;
    [[nodiscard]] std::expected<std::vector<ProbeInfo>, int> listProbes(ProbeHosts hosts) override;
    [[nodiscard]] int selectBySerialNumber(std::uint32_t serialNumber) override;
    [[nodiscard]] std::optional<std::string> open() override;
    void close() override;
    [[nodiscard]] bool isOpen() override;
    [[nodiscard]] std::string execCommand(std::string_view command) override;
    [[nodiscard]] int selectInterface(TargetInterface targetInterface) override;
    void setSpeed(std::uint32_t kiloHertz) override;
    [[nodiscard]] int connect() override;
    [[nodiscard]] bool isConnected() override;
    [[nodiscard]] std::optional<HardwareStatus> hardwareStatus() override;
    [[nodiscard]] std::string firmwareString() override;
    [[nodiscard]] std::expected<std::string, int>
    embeddedFirmwareString(std::string_view firmwareId) override;
    [[nodiscard]] std::uint32_t updateFirmwareIfNewer() override;
    [[nodiscard]] std::uint32_t serialNumber() override;
    void setResetDelay(int milliseconds) override;
    [[nodiscard]] int reset() override;
    [[nodiscard]] int halt() override;
    void go() override;
    [[nodiscard]] int rttStart() override;
    int rttStop() override;
    [[nodiscard]] int rttBufferCount(RttDirection direction) override;
    [[nodiscard]] std::expected<RttBufferInfo, int> rttBufferInfo(int index,
                                                                  RttDirection direction) override;
    [[nodiscard]] int rttRead(int index, std::span<std::byte> buffer) override;
    [[nodiscard]] int rttWrite(int index, std::span<const std::byte> data) override;
    void flushMessages() override;
    [[nodiscard]] std::string takeErrorText() override;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace hio::jlink

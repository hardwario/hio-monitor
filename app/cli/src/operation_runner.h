#pragma once

// The session and its operations behind the one-shot `chester app|lte` commands (M2 flash,
// erase, reset, recover, pib/uicr read; M6 target commands; plan/features/0013 B4: moved here
// from flash_commands.cpp). A flash-style session (attachRtt = false, no auto-reconnect) is opened
// on the command's target; operations run one at a time with progress through ProgressUi; Ctrl+C
// (Context::stop) cancels the running operation (exit 130). Owner thread only.

#include "cli_app.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "probe_options.h"
#include "progress_ui.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hio::cli {

// The CHESTER profile and one of its targets ("app", "lte"); the target must exist.
[[nodiscard]] const device::DeviceProfile& chesterProfile();
[[nodiscard]] const device::TargetProfile& chesterTarget(std::string_view targetId);

struct OperationOutcome {
    device::OperationResult result;
    std::uint32_t probeSerial = 0;
};

using StartFn = std::function<Result<device::OperationId>(device::DeviceSession&)>;

// One session for several operations in a row (e.g. a chunked memory read).
class OperationRunner {
    struct Passkey {
        explicit Passkey() = default;
    };

public:
    // Locates the J-Link library and opens the session on `targetId` (non-blocking, as
    // DeviceSession::open). `nrfUtil`: the nRF Util for modem updates. Errors: those of
    // locateJLinkLibrary, the speed option and DeviceSession::open.
    [[nodiscard]] static Result<std::unique_ptr<OperationRunner>>
    open(const Context& context, const GlobalOptions& globals, const ProbeOptions& group,
         const ProbeOptions& own, std::string_view targetId, ProgressUi& ui,
         std::optional<std::filesystem::path> nrfUtil = std::nullopt);

    OperationRunner(Passkey key, const Context& context, ProgressUi& ui, std::string_view targetId);
    ~OperationRunner();
    OperationRunner(const OperationRunner&) = delete;
    OperationRunner& operator=(const OperationRunner&) = delete;
    OperationRunner(OperationRunner&&) = delete;
    OperationRunner& operator=(OperationRunner&&) = delete;

    // Starts one operation and waits for its result with progress ("Using J-Link N" is announced
    // once). A failed operation closes the session (later calls fail with `cancelled`); review
    // R13: when the wait gave up after Ctrl+C but the worker still finished the operation, that
    // real result is returned.
    [[nodiscard]] Result<device::OperationResult> run(const StartFn& start);

    // Releases the probe (and its lock) now; joins the worker's current step. Idempotent.
    void close();

    // The serial number of the probe used by the last run() (0 before).
    [[nodiscard]] std::uint32_t probeSerial() const noexcept { return m_probeSerial; }

    // "Target: nRF52840 (app)" (human mode) when `result` identified the part.
    void showTarget(const device::OperationResult& result);

private:
    void announce();

    const Context* m_context;
    ProgressUi* m_ui;
    std::string m_targetId;
    device::SessionRegistry m_registry;
    std::unique_ptr<device::DeviceSession> m_session; // declared after the registry it uses
    bool m_announced = false;
    std::uint32_t m_probeSerial = 0;
};

// Opens a flash-only session (attachRtt = false) on `targetId`, starts the operation and waits for
// its result with progress. Ctrl+C cancels the operation (exit 130); the session is closed (the
// probe released) before returning.
[[nodiscard]] Result<OperationOutcome>
runOperation(const Context& context, const GlobalOptions& globals, const ProbeOptions& group,
             const ProbeOptions& own, std::string_view targetId, ProgressUi& ui,
             const StartFn& start, std::optional<std::filesystem::path> nrfUtil = std::nullopt);

} // namespace hio::cli

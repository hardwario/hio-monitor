#include "signals.h"

#include <atomic>
#include <cstdlib>
#include <stop_token>
#include <utility>

#ifdef _WIN32
// NOMINMAX and WIN32_LEAN_AND_MEAN are set by app/cli/CMakeLists.txt.
#include <mutex>
#include <optional>
#include <windows.h>
#else
#include <chrono>
#include <csignal> // sigaction (POSIX, declared by <signal.h>)
#include <thread>
#endif

namespace hio::cli {

namespace {

// Process-wide state shared with the OS signal handlers. Owner: the single live SignalGuard.
// The function-local statics are constant-initialized (no lazy-initialization guard), so using
// them from a signal handler is safe.
std::atomic<bool>& guardActive() {
    static std::atomic<bool> active{false};
    return active;
}

#ifdef _WIN32

struct ConsoleState {
    std::mutex mutex;                           // guards both members below
    std::optional<std::stop_source> stopSource; // copy of the guard's source
    int ctrlEvents = 0;
};

ConsoleState& consoleState() {
    static ConsoleState state;
    return state;
}

BOOL WINAPI onConsoleCtrl(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT: {
        ConsoleState& state = consoleState();
        const std::scoped_lock lock(state.mutex);
        if (!state.stopSource || state.ctrlEvents > 0) {
            return FALSE; // second event: the default handler terminates the process
        }
        ++state.ctrlEvents;
        state.stopSource->request_stop();
        return TRUE;
    }
    default:
        return FALSE;
    }
}

#else

static_assert(std::atomic<bool>::is_always_lock_free,
              "the signal flag must be lock-free to be used in a signal handler");

struct PosixState {
    std::atomic<bool> signalled{false}; // set by the signal handler, read by the watcher
    struct sigaction previousInt{};     // restored by ~SignalGuard
    struct sigaction previousTerm{};
};

PosixState& posixState() {
    static PosixState state;
    return state;
}

extern "C" void onSignal(int /*signal*/) {
    posixState().signalled.store(true, std::memory_order_relaxed);
}

#endif

} // namespace

SignalGuard::SignalGuard(std::stop_source source) : m_source(std::move(source)) {
    if (guardActive().exchange(true)) {
        std::abort(); // contract violation: only one SignalGuard at a time
    }
#ifdef _WIN32
    {
        ConsoleState& state = consoleState();
        const std::scoped_lock lock(state.mutex);
        state.stopSource = m_source;
        state.ctrlEvents = 0;
    }
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
#else
    PosixState& state = posixState();
    state.signalled.store(false);
    struct sigaction action{};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND; // the second signal gets the default action (forced exit)
    sigaction(SIGINT, &action, &state.previousInt);
    sigaction(SIGTERM, &action, &state.previousTerm);

    m_watcher = std::jthread([source = m_source](const std::stop_token& own) mutable {
        constexpr std::chrono::milliseconds pollInterval{50};
        while (!own.stop_requested()) {
            if (posixState().signalled.exchange(false)) {
                source.request_stop();
            }
            std::this_thread::sleep_for(pollInterval);
        }
    });
#endif
}

SignalGuard::~SignalGuard() {
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleCtrl, FALSE);
    {
        ConsoleState& state = consoleState();
        const std::scoped_lock lock(state.mutex);
        state.stopSource.reset();
    }
#else
    PosixState& state = posixState();
    sigaction(SIGINT, &state.previousInt, nullptr);
    sigaction(SIGTERM, &state.previousTerm, nullptr);
    m_watcher.request_stop();
    if (m_watcher.joinable()) {
        m_watcher.join();
    }
#endif
    guardActive().store(false);
}

} // namespace hio::cli

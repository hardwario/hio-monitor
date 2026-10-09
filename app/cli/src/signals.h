#pragma once

// Ctrl+C / SIGTERM handling for the CLI (plan/0006-cli.md principle 6).
//
// SignalGuard (RAII) turns the first SIGINT/SIGTERM (Windows: Ctrl+C, Ctrl+Break, console close)
// into stop_source::request_stop(); running operations observe their stop_token and end with
// `cancelled` (exit 130). A second Ctrl+C uses the default handler, i.e. forces the exit.
//
// POSIX: sigaction() with SA_RESETHAND sets a lock-free std::atomic<bool>; an owned std::jthread
// polls it every 50 ms and calls request_stop() (a signal handler must not do more).
// Windows: SetConsoleCtrlHandler; the handler runs on its own thread and calls request_stop().
// The destructor restores the previous handlers and joins the watcher thread.
// Only one SignalGuard may exist at a time (process-wide handlers).

#include <stop_token>
#include <thread>

namespace hio::cli {

class SignalGuard {
public:
    explicit SignalGuard(std::stop_source source);
    ~SignalGuard();
    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;
    SignalGuard(SignalGuard&&) = delete;
    SignalGuard& operator=(SignalGuard&&) = delete;

private:
    std::stop_source m_source;
    std::jthread m_watcher; // POSIX only; joined in the destructor
};

} // namespace hio::cli

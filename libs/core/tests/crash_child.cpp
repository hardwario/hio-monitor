// Test helper of crash_handler_test.cpp (plan/features/0015 B1): installs the crash handler into
// argv[1] and crashes in the way argv[2] names.
//   POSIX:   segv (a real invalid write), abort, bus, ill, fpe (raised signals)
//   Windows: segv (a real access violation), illegal, div0 (raised SEH exceptions)
// Exit code 2 = bad arguments, 3 = install failed, 4 = did not crash.

#include "hio/core/crash_handler.h"

#include <QtSystemDetection>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace {

// A write through a pointer the optimizer cannot prove null (read from a volatile variable).
[[gnu::noinline]] void invalidWrite(int* target) { *target = 42; }

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 3) {
        std::cerr << "usage: hio_crash_child <directory> <kind>\n";
        return 2;
    }
#ifdef Q_OS_WIN
    // No Windows Error Reporting dialog for the deliberate crash.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#else
    // No core dump from the deliberate crash.
    const rlimit noCore{.rlim_cur = 0, .rlim_max = 0};
    setrlimit(RLIMIT_CORE, &noCore);
#endif
    const auto installed = hio::crash::install({.directory = std::filesystem::path(args[1]),
                                                .appName = "crashtest",
                                                .version = "1.2.3+test"});
    if (!installed || !hio::crash::isInstalled()) {
        std::cerr << "install failed\n";
        return 3;
    }
    const std::string_view kind = args[2];
    int* volatile nowhere = nullptr;
    if (kind == "segv") {
        invalidWrite(nowhere);
    }
#ifdef Q_OS_WIN
    if (kind == "illegal") {
        RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, 0, 0, nullptr);
    } else if (kind == "div0") {
        RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, nullptr);
    }
#else
    if (kind == "abort") {
        std::abort();
    } else if (kind == "bus") {
        std::raise(SIGBUS);
    } else if (kind == "ill") {
        std::raise(SIGILL);
    } else if (kind == "fpe") {
        std::raise(SIGFPE);
    }
#endif
    std::cerr << "did not crash\n";
    return 4;
}

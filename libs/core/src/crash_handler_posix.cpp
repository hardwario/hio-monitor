// Crash handler for Linux and macOS (ADR-0123, plan/features/0015 B1).
//
// Exceptions to the code rules, confined to this file (plan 0015 "Crash handler"):
//   - C function pointers and raw pointers for the sigaction handler (a C callback);
//   - process-wide state in namespace-scope variables (the handler cannot get a context pointer);
//   - std::array buffers and no allocation, no locks, no Qt and no std::string in the handler
//     (async-signal safety: only open/openat/write/close/time/sigaction/raise/backtrace);
//   - open()/openat() are C varargs functions (NOLINT with reason);
//   - the sigaction union members (NOLINT with reason).
// backtrace() is called once by install() so that its lazy loading (which may allocate) happens
// before a crash.

#include "hio/core/crash_handler.h"
#include "hio/core/error.h"
#include "hio/core/result.h"

#include <QString>
#include <QtEnvironmentVariables>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <execinfo.h>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

namespace hio::crash {

namespace {

// Sanitizer builds (HIO_SANITIZER) install nothing; the code is compiled in every build.
#ifdef HIO_SANITIZER
constexpr bool sanitizerBuild = true;
#else
constexpr bool sanitizerBuild = false;
#endif

constexpr std::array handledSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
constexpr std::size_t maxFrames = 128;
constexpr std::size_t altStackBytes = 64ULL * 1024;

// Process-wide handler state, written once by install() before the handlers are active.
struct HandlerState {
    int directoryFd = -1;
    std::array<char, 64> app{};
    std::size_t appLength = 0;
    std::array<char, 160> version{};
    std::size_t versionLength = 0;
};
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): the signal handler has no
// context pointer; this state is written once by install() before the handlers are installed.
HandlerState state;
std::atomic_flag handling = ATOMIC_FLAG_INIT;
std::atomic<bool> installed{false};
alignas(16) std::array<std::byte, altStackBytes> altStack{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// ---- async-signal-safe helpers -----------------------------------------------------------------

// A fixed buffer with bounded appends.
template <std::size_t N> struct Text {
    std::array<char, N> data{};
    std::size_t size = 0;

    void append(std::string_view text) {
        for (const char c : text) {
            if (size >= N) {
                return;
            }
            data.at(size++) = c;
        }
    }
    void appendChars(std::span<const char> text) {
        append(std::string_view(text.data(), text.size()));
    }
    void appendUnsigned(std::uint64_t value, unsigned base = 10, std::size_t minDigits = 1) {
        std::array<char, 24> digits{};
        std::size_t count = 0;
        do {
            const auto digit = static_cast<unsigned>(value % base);
            digits.at(count++) = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
            value /= base;
        } while ((value != 0 || count < minDigits) && count < digits.size());
        while (count > 0) {
            append(std::string_view(&digits.at(--count), 1));
        }
    }
    [[nodiscard]] std::string_view view() const { return {data.data(), size}; }
};

void writeAll(int fd, std::string_view text) {
    while (!text.empty()) {
        const ssize_t written = ::write(fd, text.data(), text.size());
        if (written <= 0) {
            return; // EINTR is not retried: the process is going down anyway
        }
        text.remove_prefix(static_cast<std::size_t>(written));
    }
}

// Days since 1970-01-01 -> civil date (H. Hinnant's algorithm; integer only).
void civilFromDays(std::int64_t days, std::int64_t& year, unsigned& month, unsigned& day) {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto dayOfEra = static_cast<unsigned>(days - (era * 146097));
    const unsigned yearOfEra =
        (dayOfEra - (dayOfEra / 1460) + (dayOfEra / 36524) - (dayOfEra / 146096)) / 365;
    const unsigned dayOfYear = dayOfEra - ((365 * yearOfEra) + (yearOfEra / 4) - (yearOfEra / 100));
    const unsigned monthPrime = ((5 * dayOfYear) + 2) / 153;
    day = dayOfYear - (((153 * monthPrime) + 2) / 5) + 1;
    month = monthPrime < 10 ? monthPrime + 3 : monthPrime - 9;
    year = static_cast<std::int64_t>(yearOfEra) + (era * 400) + (month <= 2 ? 1 : 0);
}

// "YYYYMMDD-HHMMSS" (compact = true) or "YYYY-MM-DDTHH:MM:SSZ".
template <std::size_t N> void appendUtc(Text<N>& text, std::time_t now, bool compact) {
    const std::int64_t seconds = now;
    const std::int64_t days = (seconds >= 0 ? seconds : seconds - 86399) / 86400;
    const std::int64_t secondOfDay = seconds - (days * 86400);
    std::int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, year, month, day);
    text.appendUnsigned(static_cast<std::uint64_t>(year), 10, 4);
    text.append(compact ? "" : "-");
    text.appendUnsigned(month, 10, 2);
    text.append(compact ? "" : "-");
    text.appendUnsigned(day, 10, 2);
    text.append(compact ? "-" : "T");
    text.appendUnsigned(static_cast<std::uint64_t>(secondOfDay / 3600), 10, 2);
    text.append(compact ? "" : ":");
    text.appendUnsigned(static_cast<std::uint64_t>((secondOfDay / 60) % 60), 10, 2);
    text.append(compact ? "" : ":");
    text.appendUnsigned(static_cast<std::uint64_t>(secondOfDay % 60), 10, 2);
    text.append(compact ? "" : "Z");
}

std::string_view signalName(int signal) {
    switch (signal) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGABRT:
        return "SIGABRT";
    default:
        return "signal";
    }
}

int openAt(const char* name) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): openat() is a POSIX varargs function.
    return ::openat(state.directoryFd, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
}

void restoreDefault(int signal) {
    struct sigaction action{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): sigaction's handler union.
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    ::sigaction(signal, &action, nullptr);
}

void handleCrash(int signal, siginfo_t* info, void* /*context*/) {
    if (!handling.test_and_set()) {
        const std::time_t now = std::time(nullptr);
        Text<256> name;
        name.append("crash-");
        name.appendChars(std::span<const char>(state.app.data(), state.appLength));
        name.append("-");
        appendUtc(name, now, true);
        name.append(".txt");
        name.append(std::string_view("\0", 1));

        const int fd = openAt(name.data.data());
        if (fd >= 0) {
            Text<1024> header;
            header.append("HARDWARIO Monitor crash report\napplication: ");
            header.appendChars(std::span<const char>(state.app.data(), state.appLength));
            header.append("\nversion: ");
            header.appendChars(std::span<const char>(state.version.data(), state.versionLength));
            header.append("\ntime: ");
            appendUtc(header, now, false);
            header.append("\nsignal: ");
            header.appendUnsigned(static_cast<std::uint64_t>(signal));
            header.append(" (");
            header.append(signalName(signal));
            header.append(")\naddress: 0x");
            header.appendUnsigned(
                info != nullptr ? std::bit_cast<std::uintptr_t>(info->si_addr) : 0, 16);
            header.append("\n\nbacktrace:\n");
            writeAll(fd, header.view());
            std::array<void*, maxFrames> frames{};
            const int count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
            ::backtrace_symbols_fd(frames.data(), count, fd);
            ::close(fd);
        }
        const int marker = openAt("last-crash");
        if (marker >= 0) {
            writeAll(marker, name.view().substr(0, name.size - 1));
            writeAll(marker, "\n");
            ::close(marker);
        }
    }
    restoreDefault(signal);
    ::raise(signal);
}

bool copyText(std::string_view text, std::span<char> target, std::size_t& length) {
    if (text.empty() || text.size() > target.size()) {
        return false;
    }
    std::ranges::copy(text, target.begin());
    length = text.size();
    return true;
}

} // namespace

Result<void> install(const Config& config) {
    if (sanitizerBuild) {
        return {}; // the sanitizers install their own handlers
    }
    if (qEnvironmentVariable("HIO_NO_CRASH_HANDLER") == QStringLiteral("1")) {
        return {};
    }
    if (installed.load()) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "crash handler installed twice"));
    }
    if (!copyText(config.appName, state.app, state.appLength) ||
        !copyText(config.version, state.version, state.versionLength)) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "crash handler: invalid name or version"));
    }
    std::error_code error;
    std::filesystem::create_directories(config.directory, error);
    const std::string directory = config.directory.string();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open() is a POSIX varargs function.
    state.directoryFd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (state.directoryFd < 0) {
        return std::unexpected(makeError(ErrorCode::IoWriteFailed, "crash directory " + directory));
    }
    pruneOld(config.directory);

    std::array<void*, 1> warmUp{};
    (void)::backtrace(warmUp.data(), static_cast<int>(warmUp.size()));

    stack_t stack{};
    stack.ss_sp = altStack.data();
    stack.ss_size = altStack.size();
    stack.ss_flags = 0;
    ::sigaltstack(&stack, nullptr);

    for (const int signal : handledSignals) {
        struct sigaction action{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): sigaction's handler union.
        action.sa_sigaction = handleCrash;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        ::sigaction(signal, &action, nullptr);
    }
    installed.store(true);
    return {};
}

bool isInstalled() noexcept { return installed.load(); }

} // namespace hio::crash

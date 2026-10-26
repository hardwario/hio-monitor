// Crash handler for Windows (ADR-0123, plan/features/0015 B1): SetUnhandledExceptionFilter +
// MiniDumpWriteDump (dbghelp, a system DLL linked normally - not a vendor SDK).
//
// Exceptions to the code rules, confined to this file (plan 0015 "Crash handler"):
//   - Win32 C API types (HANDLE, raw pointers) in the exception filter (a C callback);
//   - process-wide state in namespace-scope variables (the filter has no context pointer);
//   - std::array buffers; the filter allocates nothing itself (the file name is built into a
//     fixed buffer, the directory was converted by install()).
// After the dump the filter returns EXCEPTION_CONTINUE_SEARCH, so Windows Error Reporting still
// runs.

// NOMINMAX and WIN32_LEAN_AND_MEAN are set by libs/core/CMakeLists.txt.
#include <windows.h>
// <windows.h> must come first: <dbghelp.h> needs its types. This comment ends the include block,
// so clang-format (IncludeBlocks: Regroup) does not sort it below.
#include "hio/core/crash_handler.h"
#include "hio/core/error.h"
#include "hio/core/result.h"

#include <QString>
#include <QtEnvironmentVariables>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <dbghelp.h>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace hio::crash {

namespace {

// Sanitizer builds (HIO_SANITIZER) install nothing; the code is compiled in every build.
#ifdef HIO_SANITIZER
constexpr bool sanitizerBuild = true;
#else
constexpr bool sanitizerBuild = false;
#endif

// Process-wide filter state, written once by install() before the filter is set.
struct FilterState {
    std::array<wchar_t, 1024> directory{}; // with a trailing backslash
    std::size_t directoryLength = 0;
    std::array<wchar_t, 64> app{};
    std::size_t appLength = 0;
};
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): the exception filter has no
// context pointer; this state is written once by install() before the filter is set.
FilterState state;
std::atomic_flag handling = ATOMIC_FLAG_INIT;
std::atomic<bool> installed{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

template <std::size_t N> struct WideText {
    std::array<wchar_t, N> data{};
    std::size_t size = 0;

    void append(const wchar_t* text, std::size_t length) {
        for (std::size_t i = 0; i < length && size + 1 < N; ++i) {
            data.at(size++) = std::wstring_view(text, length).at(i);
        }
        data.at(size) = L'\0';
    }
    void append(std::wstring_view text) { append(text.data(), text.size()); }
    void appendNumber(unsigned value, std::size_t digits) {
        std::array<wchar_t, 8> buffer{};
        for (std::size_t i = digits; i > 0; --i) {
            buffer.at(i - 1) = static_cast<wchar_t>(L'0' + (value % 10));
            value /= 10;
        }
        append(buffer.data(), digits);
    }
};

HANDLE createFile(const wchar_t* path) {
    return CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
}

LONG WINAPI handleCrash(EXCEPTION_POINTERS* exception) {
    if (handling.test_and_set()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    SYSTEMTIME now{};
    GetSystemTime(&now);
    WideText<1200> name;
    name.append(state.directory.data(), state.directoryLength);
    const std::size_t nameStart = name.size;
    name.append(L"crash-");
    name.append(state.app.data(), state.appLength);
    name.append(L"-");
    name.appendNumber(now.wYear, 4);
    name.appendNumber(now.wMonth, 2);
    name.appendNumber(now.wDay, 2);
    name.append(L"-");
    name.appendNumber(now.wHour, 2);
    name.appendNumber(now.wMinute, 2);
    name.appendNumber(now.wSecond, 2);
    name.append(L".dmp");

    const HANDLE file = createFile(name.data.data());
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
                          exception != nullptr ? &info : nullptr, nullptr, nullptr);
        CloseHandle(file);
    }
    WideText<1100> marker;
    marker.append(state.directory.data(), state.directoryLength);
    marker.append(L"last-crash");
    const HANDLE markerFile = createFile(marker.data.data());
    if (markerFile != INVALID_HANDLE_VALUE) {
        // The file name (ASCII: "crash-<app>-<UTC>.dmp").
        std::array<char, 128> ascii{};
        DWORD length = 0;
        for (std::size_t i = nameStart; i < name.size && length + 1 < ascii.size(); ++i) {
            ascii.at(length++) = static_cast<char>(name.data.at(i));
        }
        ascii.at(length++) = '\n';
        DWORD written = 0;
        WriteFile(markerFile, ascii.data(), length, &written, nullptr);
        CloseHandle(markerFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
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
    std::error_code error;
    std::filesystem::create_directories(config.directory, error);
    if (!std::filesystem::is_directory(config.directory, error)) {
        return std::unexpected(
            makeError(ErrorCode::IoWriteFailed, "crash directory " + config.directory.string()));
    }
    std::wstring directory = config.directory.wstring();
    if (!directory.ends_with(L'\\')) {
        directory += L'\\';
    }
    if (directory.size() + 64 > state.directory.size() || config.appName.empty() ||
        config.appName.size() > state.app.size()) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "crash handler: path or name too long"));
    }
    for (std::size_t i = 0; i < directory.size(); ++i) {
        state.directory.at(i) = directory.at(i);
    }
    state.directoryLength = directory.size();
    for (std::size_t i = 0; i < config.appName.size(); ++i) {
        state.app.at(i) = static_cast<wchar_t>(config.appName.at(i));
    }
    state.appLength = config.appName.size();
    pruneOld(config.directory);
    SetUnhandledExceptionFilter(handleCrash);
    installed.store(true);
    return {};
}

bool isInstalled() noexcept { return installed.load(); }

} // namespace hio::crash

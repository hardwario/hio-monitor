// Windows process source (ADR-0073): CreateToolhelp32Snapshot + Process32FirstW / Process32NextW.
// Only the executable file names are listed (the full path would need OpenProcess per process).

#include <QtSystemDetection>

#ifdef Q_OS_WIN

// NOMINMAX and WIN32_LEAN_AND_MEAN are set by libs/diag/CMakeLists.txt.
#include <windows.h>
// <windows.h> must come first: <tlhelp32.h> needs its types (HANDLE, DWORD, WCHAR). This comment
// ends the include block, so clang-format (IncludeBlocks: Regroup) does not sort it below.
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "platform/process_source.h"

#include <QString>
#include <QtTypes>

#include <cstddef>
#include <expected>
#include <memory>
#include <stop_token>
#include <string>
#include <tlhelp32.h>
#include <utility>
#include <vector>

namespace hio::diag::platform {

namespace {

// Closes the snapshot handle (RAII).
class SnapshotHandle {
public:
    explicit SnapshotHandle(HANDLE handle) : m_handle(handle) {}
    ~SnapshotHandle() {
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
    }
    SnapshotHandle(const SnapshotHandle&) = delete;
    SnapshotHandle& operator=(const SnapshotHandle&) = delete;
    SnapshotHandle(SnapshotHandle&&) = delete;
    SnapshotHandle& operator=(SnapshotHandle&&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }

private:
    HANDLE m_handle;
};

class WindowsProcessSource final : public IProcessSource {
public:
    Result<std::vector<Process>> list(std::size_t maxCount, const std::stop_token& stop) override {
        const SnapshotHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (snapshot.get() == INVALID_HANDLE_VALUE) {
            return std::unexpected(
                makeError(ErrorCode::IoReadFailed, "CreateToolhelp32Snapshot failed"));
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = static_cast<DWORD>(sizeof(entry));
        std::vector<Process> processes;
        for (BOOL ok = Process32FirstW(snapshot.get(), &entry);
             ok != FALSE && processes.size() < maxCount;
             ok = Process32NextW(snapshot.get(), &entry)) {
            if (stop.stop_requested()) {
                return std::unexpected(makeError(ErrorCode::Timeout, "process list incomplete"));
            }
            Process process;
            process.pid = static_cast<qint64>(entry.th32ProcessID);
            process.name = QString::fromWCharArray(&entry.szExeFile[0]).toStdString();
            processes.push_back(std::move(process));
        }
        return processes;
    }
};

} // namespace

std::shared_ptr<IProcessSource> makeProcessSource() {
    return std::make_shared<WindowsProcessSource>();
}

} // namespace hio::diag::platform

#endif

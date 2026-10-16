// Linux process source (ADR-0073): /proc/<pid>/comm and the /proc/<pid>/exe link.

#include "hio/diag/process_scan.h"

#include <QtSystemDetection>

#include <utility>

#ifdef Q_OS_LINUX

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "platform/process_source.h"

#include <QString>
#include <QtTypes>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <stop_token>
#include <string>
#include <system_error>
#include <vector>

namespace hio::diag::platform {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t maxCommBytes = 64;

class LinuxProcessSource final : public IProcessSource {
public:
    explicit LinuxProcessSource(fs::path procRoot) : m_procRoot(std::move(procRoot)) {}

    Result<std::vector<Process>> list(std::size_t maxCount, const std::stop_token& stop) override {
        std::error_code error;
        fs::directory_iterator it(m_procRoot, error);
        if (error) {
            return std::unexpected(
                makeError(ErrorCode::IoReadFailed,
                          std::format("{}: {}", toUtf8(m_procRoot), error.message())));
        }
        std::vector<Process> processes;
        for (; it != fs::directory_iterator() && processes.size() < maxCount; it.increment(error)) {
            if (error) {
                break;
            }
            if (stop.stop_requested()) {
                return std::unexpected(makeError(ErrorCode::Timeout, "process list incomplete"));
            }
            const std::string name = it->path().filename().string();
            bool numeric = false;
            const qint64 pid = QString::fromStdString(name).toLongLong(&numeric);
            if (!numeric) {
                continue;
            }
            Process process;
            process.pid = pid;
            process.name = readComm(it->path() / "comm");
            std::error_code linkError;
            const fs::path exe = fs::read_symlink(it->path() / "exe", linkError);
            if (!linkError) {
                process.executablePath = exe.string();
            }
            if (!process.name.empty() || !process.executablePath.empty()) {
                processes.push_back(std::move(process));
            }
        }
        return processes;
    }

private:
    static std::string readComm(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::array<char, maxCommBytes> buffer{};
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::string text(buffer.data(), static_cast<std::size_t>(file.gcount()));
        while (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
            text.pop_back();
        }
        return text;
    }

    fs::path m_procRoot;
};

} // namespace

std::shared_ptr<IProcessSource> makeProcessSource() {
    return std::make_shared<LinuxProcessSource>("/proc");
}

} // namespace hio::diag::platform

#endif

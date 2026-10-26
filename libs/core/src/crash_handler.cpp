// Crash files: pruning, listing and the marker (ADR-0123, plan/features/0015 B1). The handlers
// themselves are in crash_handler_posix.cpp / crash_handler_win.cpp.

#include "hio/core/crash_handler.h"

#include <QStandardPaths>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::crash {

namespace {

namespace fs = std::filesystem;

struct CrashFile {
    fs::path path;
    fs::file_time_type time;
    std::uintmax_t size = 0;
};

// The crash files of `directory`, newest first (bounded: at most maxEntries directory entries).
std::vector<CrashFile> crashFiles(const fs::path& directory) {
    constexpr std::size_t maxEntries = 1024;
    std::vector<CrashFile> files;
    std::error_code error;
    fs::directory_iterator it(directory, error);
    std::size_t entries = 0;
    for (; !error && it != fs::directory_iterator() && entries < maxEntries;
         it.increment(error), ++entries) {
        const std::string name = it->path().filename().string();
        if (!isCrashFileName(name)) {
            continue;
        }
        std::error_code fileError;
        const auto time = fs::last_write_time(it->path(), fileError);
        const auto size = fs::file_size(it->path(), fileError);
        if (fileError) {
            continue;
        }
        files.push_back({.path = it->path(), .time = time, .size = size});
    }
    // Newest first; the name (crash-<app>-<UTC>) breaks ties.
    std::ranges::sort(files, [](const CrashFile& a, const CrashFile& b) {
        if (a.time != b.time) {
            return a.time > b.time;
        }
        return a.path.filename() > b.path.filename();
    });
    return files;
}

} // namespace

fs::path defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "crashes";
}

bool isCrashFileName(std::string_view name) noexcept {
    return name.starts_with("crash-") && (name.ends_with(".txt") || name.ends_with(".dmp"));
}

void pruneOld(const fs::path& directory) {
    std::size_t kept = 0;
    for (const CrashFile& file : crashFiles(directory)) {
        const bool dump = file.path.extension() == ".dmp";
        const bool oversize = file.size > (dump ? maxDumpBytes : maxTextBytes);
        if (oversize || kept >= maxCrashFiles) {
            std::error_code error;
            fs::remove(file.path, error);
            continue;
        }
        ++kept;
    }
}

std::vector<fs::path> recent(const fs::path& directory, std::size_t max) {
    std::vector<fs::path> paths;
    for (CrashFile& file : crashFiles(directory)) {
        if (paths.size() >= max) {
            break;
        }
        paths.push_back(std::move(file.path));
    }
    return paths;
}

bool crashedLastRun(const fs::path& directory) {
    std::error_code error;
    return fs::exists(directory / markerFileName, error);
}

void acknowledge(const fs::path& directory) {
    std::error_code error;
    fs::remove(directory / markerFileName, error);
}

} // namespace hio::crash

#include "hio/hw/jlink/library_locator.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::jlink {

namespace {

namespace fs = std::filesystem;

struct Candidate {
    fs::path path;
    std::uint32_t version = 0;
};

std::uint32_t versionOf(unsigned long major, unsigned long minor, unsigned long revision) {
    return static_cast<std::uint32_t>((major * 10000U) + (minor * 100U) + revision);
}

bool isLibraryFileName(const std::string& name) {
#ifdef _WIN32
    std::string lower = name;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
        return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    });
#ifdef _WIN64
    return lower == "jlink_x64.dll";
#else
    return lower == "jlinkarm.dll";
#endif
#elifdef __APPLE__
    return name.starts_with("libjlinkarm") && name.ends_with(".dylib");
#else
    return name.starts_with("libjlinkarm.so");
#endif
}

// Library files directly inside `directory` (symlinks are followed by the caller's dedupe).
std::vector<Candidate> candidatesIn(const fs::path& directory) {
    std::vector<Candidate> result;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::error_code typeError;
        if (!entry.is_regular_file(typeError) || typeError) {
            continue;
        }
        if (isLibraryFileName(toUtf8(entry.path().filename()))) {
            result.push_back(
                Candidate{.path = entry.path(), .version = versionFromPath(entry.path())});
        }
    }
    return result;
}

// "JLink", "JLink_V810a", ... (pylink's rule: any directory starting with "JLink").
bool isInstallDirectoryName(const std::string& name) { return name.starts_with("JLink"); }

std::vector<Candidate> discover(const LocatorOptions& options, std::vector<std::string>& searched) {
    std::vector<Candidate> all;
    for (const fs::path& root : options.searchRoots) {
        searched.push_back(toUtf8(root));
        std::error_code ec;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code typeError;
            if (it->is_directory(typeError) &&
                isInstallDirectoryName(toUtf8(it->path().filename()))) {
                std::ranges::move(candidatesIn(it->path()), std::back_inserter(all));
            }
        }
    }
    for (const fs::path& installPath : options.installPaths) {
        searched.push_back(toUtf8(installPath));
        std::ranges::move(candidatesIn(installPath), std::back_inserter(all));
    }
    return all;
}

// Several names may point to the same file (libjlinkarm.so -> .so.8 -> .so.8.10.1): keep one
// entry per real file with the highest version read from any of its names.
std::vector<Candidate> dedupe(std::vector<Candidate> candidates) {
    std::vector<std::pair<fs::path, Candidate>> unique;
    for (Candidate& candidate : candidates) {
        std::error_code ec;
        fs::path real = fs::canonical(candidate.path, ec);
        if (ec) {
            continue; // dangling symlink
        }
        auto it =
            std::ranges::find_if(unique, [&](const auto& item) { return item.first == real; });
        if (it == unique.end()) {
            unique.emplace_back(std::move(real), std::move(candidate));
        } else if (candidate.version > it->second.version) {
            it->second.version = candidate.version;
            it->second.path = std::move(candidate.path);
        }
    }
    std::vector<Candidate> result;
    result.reserve(unique.size());
    for (auto& item : unique) {
        result.push_back(std::move(item.second));
    }
    return result;
}

std::optional<Candidate> best(std::vector<Candidate> candidates) {
    candidates = dedupe(std::move(candidates));
    if (candidates.empty()) {
        return std::nullopt;
    }
    // Highest version first; on a tie the first found (stable).
    std::ranges::stable_sort(
        candidates, [](const Candidate& a, const Candidate& b) { return a.version > b.version; });
    return candidates.front();
}

std::string joinPaths(const std::vector<std::string>& paths) {
    std::string text;
    for (const std::string& path : paths) {
        text += text.empty() ? path : ", " + path;
    }
    return text;
}

Result<LibraryLocation> useOverride(const fs::path& path, LibrarySource source) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        if (auto found = best(candidatesIn(path))) {
            return LibraryLocation{
                .path = found->path, .versionFromName = found->version, .source = source};
        }
    } else if (fs::exists(path, ec)) {
        return LibraryLocation{
            .path = path, .versionFromName = versionFromPath(path), .source = source};
    }
    std::string origin;
    switch (source) {
    case LibrarySource::Option:
        origin = "--jlink-library";
        break;
    case LibrarySource::Environment:
        origin = std::string(libraryEnvironmentVariable);
        break;
    case LibrarySource::Setting:
        origin = "setting jlink/libraryPath";
        break;
    case LibrarySource::Discovered:
        origin = "auto-discovery";
        break;
    }
    return std::unexpected(makeError(ErrorCode::DependencyJLinkNotFound,
                                     "not found: " + toUtf8(path) + " (from " + origin + ")"));
}

std::optional<fs::path> nonEmpty(std::optional<fs::path> path) {
    if (path && path->empty()) {
        return std::nullopt;
    }
    return path;
}

} // namespace

SettingKey<fs::path> libraryPathSetting() {
    return SettingKey<fs::path>{.path = "jlink/libraryPath", .defaultValue = fs::path()};
}

LocatorOptions defaultLocatorOptions(std::optional<fs::path> option, const Settings* settings) {
    LocatorOptions options;
    options.option = nonEmpty(std::move(option));
    const QString environment =
        qEnvironmentVariable(QByteArray(libraryEnvironmentVariable.data(),
                                        static_cast<qsizetype>(libraryEnvironmentVariable.size()))
                                 .constData());
    if (!environment.isEmpty()) {
        options.environment = fs::path(environment.toStdU16String());
    }
    if (settings != nullptr) {
        options.setting = nonEmpty(settings->get(libraryPathSetting()));
    }
    options.searchRoots = defaultSearchRoots();
    options.installPaths = registryInstallPaths();
    return options;
}

std::vector<fs::path> defaultSearchRoots() {
#ifdef _WIN32
    std::vector<fs::path> roots;
    for (const char* variable : {"ProgramFiles", "ProgramW6432"}) {
        const QString programFiles = qEnvironmentVariable(variable);
        if (!programFiles.isEmpty()) {
            fs::path root = fs::path(programFiles.toStdU16String()) / "SEGGER";
            if (std::ranges::find(roots, root) == roots.end()) {
                roots.push_back(std::move(root));
            }
        }
    }
    return roots;
#elifdef __APPLE__
    return {fs::path("/Applications/SEGGER")};
#else
    return {fs::path("/opt/SEGGER")};
#endif
}

std::vector<fs::path> registryInstallPaths() {
    std::vector<fs::path> paths;
#ifdef _WIN32
    for (const char* key : {"HKEY_CURRENT_USER\\SOFTWARE\\SEGGER\\J-Link",
                            "HKEY_LOCAL_MACHINE\\SOFTWARE\\SEGGER\\J-Link"}) {
        const QSettings registry(QString::fromLatin1(key), QSettings::NativeFormat);
        const QString installPath = registry.value(QStringLiteral("InstallPath")).toString();
        if (!installPath.isEmpty()) {
            paths.emplace_back(installPath.toStdU16String());
        }
    }
#endif
    return paths;
}

Result<LibraryLocation> locateLibrary(const LocatorOptions& options) {
    if (options.option) {
        return useOverride(*options.option, LibrarySource::Option);
    }
    if (options.environment) {
        return useOverride(*options.environment, LibrarySource::Environment);
    }
    if (options.setting) {
        return useOverride(*options.setting, LibrarySource::Setting);
    }
    std::vector<std::string> searched;
    if (auto found = best(discover(options, searched))) {
        qCInfo(log::lcJLink) << "J-Link library discovered:" << toQString(toUtf8(found->path));
        return LibraryLocation{.path = found->path,
                               .versionFromName = found->version,
                               .source = LibrarySource::Discovered};
    }
    return std::unexpected(makeError(
        ErrorCode::DependencyJLinkNotFound,
        "searched: " + (searched.empty() ? std::string("(nothing)") : joinPaths(searched))));
}

std::uint32_t versionFromPath(const fs::path& path) {
    static const std::regex fileVersion(
        R"(^libjlinkarm(?:\.so)?\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?:\.dylib)?$)");
    static const std::regex directoryVersion(R"(^JLink_V(\d)(\d\d)([a-z]?)$)");
    std::smatch match;
    const std::string file = toUtf8(path.filename());
    if (std::regex_match(file, match, fileVersion)) {
        return versionOf(std::stoul(match[1].str()), std::stoul(match[2].str()),
                         std::stoul(match[3].str()));
    }
    const std::string directory = toUtf8(path.parent_path().filename());
    if (std::regex_match(directory, match, directoryVersion)) {
        const std::string letter = match[3].str();
        const unsigned long revision =
            letter.empty() ? 0UL : static_cast<unsigned long>(letter[0] - 'a' + 1);
        return versionOf(std::stoul(match[1].str()), std::stoul(match[2].str()), revision);
    }
    return 0;
}

} // namespace hio::jlink

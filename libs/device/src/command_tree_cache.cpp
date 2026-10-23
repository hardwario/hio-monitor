#include "hio/device/command_tree_cache.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/command_tree.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::device {

namespace {

namespace fs = std::filesystem;

constexpr int schemaVersion = 1;
constexpr std::string_view suffix = ".json";

QString key(const char* name) { return QString::fromLatin1(name); }

bool sameKey(const QJsonObject& json, const FirmwareKey& firmware) {
    return json.value(key("profile")).toString() == toQString(firmware.profileId) &&
           json.value(key("target")).toString() == toQString(firmware.targetId) &&
           json.value(key("fwName")).toString() == toQString(firmware.fwName) &&
           json.value(key("fwVersion")).toString() == toQString(firmware.fwVersion);
}

// Profile and target ids are compiled-in identifiers; anything else is replaced.
std::string safeId(std::string_view id) {
    std::string safe;
    for (const char c : id.substr(0, 32)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        safe += ok ? c : '_';
    }
    return safe;
}

} // namespace

CommandTreeCache::CommandTreeCache(fs::path directory) : m_directory(std::move(directory)) {}

fs::path CommandTreeCache::defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "command-trees";
}

fs::path CommandTreeCache::fileFor(const FirmwareKey& firmware) const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const std::string& part :
         {firmware.profileId, firmware.targetId, firmware.fwName, firmware.fwVersion}) {
        hash.addData(QByteArray::fromStdString(part));
        hash.addData(QByteArrayView("\n"));
    }
    const std::string digest = hash.result().toHex().left(16).toStdString();
    return m_directory / std::format("{}-{}-{}{}", safeId(firmware.profileId),
                                     safeId(firmware.targetId), digest, suffix);
}

std::optional<CommandTree> CommandTreeCache::load(const FirmwareKey& firmware) const {
    const fs::path file = fileFor(firmware);
    QFile input(toQString(toUtf8(file)));
    if (!input.exists() || !input.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    if (std::cmp_greater(input.size(), maxFileBytes)) {
        qCWarning(log::lcDevice).noquote()
            << "Command tree ignored (too large):" << toQString(toUtf8(file));
        return std::nullopt;
    }
    const QByteArray bytes = input.read(static_cast<qint64>(maxFileBytes) + 1);
    input.close();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value(key("schema")).toInt() != schemaVersion ||
        !sameKey(document.object(), firmware)) {
        qCWarning(log::lcDevice).noquote() << "Command tree ignored:" << toQString(toUtf8(file));
        return std::nullopt;
    }
    auto tree = CommandTree::fromJson(document.object().value(key("tree")).toObject());
    if (!tree) {
        qCWarning(log::lcDevice).noquote()
            << "Command tree ignored:" << toQString(tree.error().detail);
        return std::nullopt;
    }
    std::error_code ec;
    fs::last_write_time(file, fs::file_time_type::clock::now(), ec); // LRU touch
    return std::move(*tree);
}

Status CommandTreeCache::store(const FirmwareKey& firmware, const CommandTree& tree) {
    QJsonObject json;
    json.insert(key("schema"), schemaVersion);
    json.insert(key("profile"), toQString(firmware.profileId));
    json.insert(key("target"), toQString(firmware.targetId));
    json.insert(key("fwName"), toQString(firmware.fwName));
    json.insert(key("fwVersion"), toQString(firmware.fwVersion));
    json.insert(key("tree"), tree.toJson());
    const QByteArray bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    const fs::path file = fileFor(firmware);
    if (std::cmp_greater(bytes.size(), maxFileBytes)) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument,
                      std::format("command tree for {} {}: {} bytes (limit {})", firmware.fwName,
                                  firmware.fwVersion, bytes.size(), maxFileBytes)));
    }
    std::error_code ec;
    fs::create_directories(m_directory, ec);
    QLockFile lock(toQString(toUtf8(m_directory / ".lock")));
    if (!lock.tryLock(m_lockTimeout)) {
        qCWarning(log::lcDevice).noquote()
            << "Command tree not stored: the cache is locked by another program:"
            << toQString(toUtf8(m_directory));
        return {};
    }
    QSaveFile output(toQString(toUtf8(file)));
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit()) {
        return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                         toUtf8(file) + ": " + output.errorString().toStdString()));
    }
    evict(file);
    return {};
}

void CommandTreeCache::evict(const fs::path& kept) {
    struct Entry {
        fs::path path;
        fs::file_time_type modified;
    };
    std::vector<Entry> entries;
    std::error_code ec;
    for (fs::directory_iterator it(m_directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || toUtf8(it->path().extension()) != suffix ||
            it->path() == kept) {
            continue;
        }
        entries.push_back(Entry{.path = it->path(), .modified = it->last_write_time(entryError)});
    }
    std::ranges::sort(entries,
                      [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
    // The kept (new) file counts first.
    for (std::size_t i = maxFirmwares - 1; i < entries.size(); ++i) {
        std::error_code removeError;
        if (fs::remove(entries[i].path, removeError)) {
            qCInfo(log::lcDevice).noquote()
                << "Command tree evicted:" << toQString(toUtf8(entries[i].path));
        }
    }
}

} // namespace hio::device

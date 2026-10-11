#include "file_io.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::firmware::detail {

Result<std::vector<std::byte>> readFileBounded(const std::filesystem::path& path,
                                               std::uint64_t maxBytes) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(makeError(ErrorCode::IoFileNotFound, toUtf8(path)));
    }
    QFile file(toQString(toUtf8(path)));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(ErrorCode::IoReadFailed,
                                         toUtf8(path) + ": " + file.errorString().toStdString()));
    }
    const auto size = static_cast<std::uint64_t>(file.size());
    if (size > maxBytes) {
        return std::unexpected(makeError(
            ErrorCode::FirmwareInvalidImage,
            std::format("{}: {} bytes, the limit is {} bytes", toUtf8(path), size, maxBytes)));
    }
    const QByteArray data = file.read(static_cast<qint64>(size));
    if (std::cmp_not_equal(data.size(), size)) {
        return std::unexpected(makeError(ErrorCode::IoReadFailed,
                                         toUtf8(path) + ": " + file.errorString().toStdString()));
    }
    const auto bytes = std::as_bytes(std::span(data.constData(), static_cast<std::size_t>(size)));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

} // namespace hio::firmware::detail

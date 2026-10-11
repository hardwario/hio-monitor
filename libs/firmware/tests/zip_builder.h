#pragma once

// Writes zip archives for tests with miniz's heap writer (the product only reads zips).

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <cstddef>
#include <miniz.h>
#include <string>
#include <utility>
#include <vector>

namespace hio::firmware::test {

struct ZipItem {
    std::string name;
    std::string data;
};

// The archive bytes; empty on failure. `level` 0 stores, 1..10 deflates.
inline QByteArray zipBytes(const std::vector<ZipItem>& items, unsigned level = 6) {
    mz_zip_archive archive{};
    mz_zip_zero_struct(&archive);
    if (mz_zip_writer_init_heap(&archive, 0, 0) == MZ_FALSE) {
        return {};
    }
    for (const ZipItem& item : items) {
        if (mz_zip_writer_add_mem(&archive, item.name.c_str(), item.data.data(), item.data.size(),
                                  level) == MZ_FALSE) {
            mz_zip_writer_end(&archive);
            return {};
        }
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    if (mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size) == MZ_FALSE) {
        mz_zip_writer_end(&archive);
        return {};
    }
    QByteArray bytes(static_cast<const char*>(buffer), static_cast<qsizetype>(size));
    mz_free(buffer);
    mz_zip_writer_end(&archive);
    return bytes;
}

// Replaces every occurrence of `from` by `to` (same length): entry names miniz refuses to
// write (absolute paths, drive letters) are produced by patching an archive.
inline QByteArray patched(QByteArray bytes, const QByteArray& from, const QByteArray& to) {
    return bytes.replace(from, to);
}

inline bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

} // namespace hio::firmware::test

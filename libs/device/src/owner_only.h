#pragma once

// ADR-0186 (plan/features/0025, review M-4a): files that may contain device secrets (command
// history, console files) are readable and writable by their owner only. Unix: mode 0600, set
// right after the file is opened and before the first write (an existing file is tightened when it
// is opened again). Windows: no code; the files live in the per-user AppData folder, whose
// inherited ACL is owner-only.

#include "hwio/core/json.h"
#include "hwio/core/log.h"

#include <QFileDevice>
#include <QLoggingCategory>
#include <QtLogging>
#include <QtSystemDetection>

namespace hwio::device::detail {

inline void restrictToOwner([[maybe_unused]] QFileDevice& file) {
#ifndef Q_OS_WIN
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        qCWarning(log::lcDevice).noquote()
            << "Could not make the file owner-only:" << file.fileName() << file.errorString();
    }
#endif
}

} // namespace hwio::device::detail

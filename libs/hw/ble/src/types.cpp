#include "hio/hw/ble/types.h"

#include "hio/core/error.h"

#include <QBluetoothAddress>
#include <QString>
#include <QUuid>
#include <QtTypes>

#include <optional>
#include <string>
#include <string_view>

namespace hio::ble {

std::optional<Error> adapterError(const AdapterInfo& adapter) {
    switch (adapter.state) {
    case AdapterState::Ready:
    case AdapterState::PermissionUndetermined:
        return std::nullopt;
    case AdapterState::NotFound:
        return makeError(ErrorCode::BleAdapterNotFound);
    case AdapterState::PoweredOff:
        return makeError(ErrorCode::BleAdapterOff, adapter.name);
    case AdapterState::PermissionDenied:
        return makeError(ErrorCode::BlePermissionDenied);
    case AdapterState::StackUnavailable:
        return makeError(ErrorCode::BleStackUnavailable);
    }
    return makeError(ErrorCode::BleAdapterNotFound);
}

BleDeviceId canonicalDeviceId(std::string_view id) {
    const QString text = QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())).trimmed();
    if (const QBluetoothAddress address(text); !address.isNull()) {
        return address.toString().toUpper().toStdString();
    }
    if (const QUuid uuid = QUuid::fromString(text); !uuid.isNull()) {
        return uuid.toString(QUuid::WithoutBraces).toStdString();
    }
    return text.toStdString();
}

bool sameDeviceId(std::string_view a, std::string_view b) {
    return a.size() == b.size() && toLowerAscii(a) == toLowerAscii(b);
}

std::string toLowerAscii(std::string_view text) {
    std::string lower(text);
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lower;
}

} // namespace hio::ble

#include "tool_support.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/limits.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/schema.h"
#include "hwio/mcp/tool_call.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/tools_context.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimeZone>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hwio::mcp::tools::detail {

namespace fs = std::filesystem;

namespace {

std::string utf8(const QString& text) { return text.toStdString(); }

// MCP error texts are plain English (ADR-0103, review R12).
QString tooLarge(const QString& path, std::uint64_t maxBytes) {
    return QStringLiteral("The file %1 is larger than %2 bytes.")
        .arg(path, QString::number(maxBytes));
}

} // namespace

// ---- text and JSON ------------------------------------------------------------------------------

QString qs(std::string_view utf8Text) { return hwio::toQString(utf8Text); }

QString isoTime(std::chrono::system_clock::time_point time) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}

double seconds(std::chrono::milliseconds duration) {
    return static_cast<double>(duration.count()) / 1000.0;
}

QJsonValue seqJson(std::uint64_t seq) { return {static_cast<qint64>(seq)}; }

QJsonObject connectionJson(const hwio::device::ConnectionEvent& event) {
    QJsonObject json;
    json.insert(QStringLiteral("state"), qs(hwio::device::connectionStateId(event.state)));
    json.insert(QStringLiteral("error"), event.error ? QJsonValue(hwio::toJson(*event.error))
                                                     : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("target_mv"), event.targetMillivolts
                                                 ? QJsonValue(*event.targetMillivolts)
                                                 : QJsonValue(QJsonValue::Null));
    return json;
}

QJsonObject deviceEntryJson(const DeviceEntry& entry) {
    QJsonObject json;
    json.insert(QStringLiteral("id"), qs(entry.id));
    json.insert(QStringLiteral("kind"), qs(entry.kind));
    json.insert(QStringLiteral("display_name"), qs(entry.displayName));
    json.insert(QStringLiteral("profile"),
                entry.profileId.empty() ? QJsonValue(QJsonValue::Null) : qs(entry.profileId));
    QString state;
    switch (entry.state) {
    case DeviceEntry::State::Open:
        state = QStringLiteral("open");
        break;
    case DeviceEntry::State::Available:
        state = QStringLiteral("available");
        break;
    case DeviceEntry::State::InUseByOtherProcess:
        state = QStringLiteral("in_use_by_other_process");
        break;
    }
    json.insert(QStringLiteral("state"), state);
    if (entry.owner) {
        QJsonObject owner;
        owner.insert(QStringLiteral("app"), qs(entry.owner->app));
        owner.insert(QStringLiteral("pid"), static_cast<qint64>(entry.owner->pid));
        json.insert(QStringLiteral("owner"), owner);
    } else {
        json.insert(QStringLiteral("owner"), QJsonValue(QJsonValue::Null));
    }
    json.insert(QStringLiteral("connection"), entry.connection
                                                  ? QJsonValue(connectionJson(*entry.connection))
                                                  : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("opened_by_mcp"), entry.openedByMcp);
    return json;
}

QJsonObject deviceStatusJson(const std::string& id, const DeviceRef& ref) {
    QJsonObject json;
    json.insert(QStringLiteral("id"), qs(id));
    if (!ref.alive()) {
        return json;
    }
    const hwio::device::ConsoleSession& session = *ref.console;
    const hwio::device::ConsoleLog& log = session.log();
    json.insert(QStringLiteral("connection"), connectionJson(session.connection()));
    json.insert(QStringLiteral("terminal_total"), seqJson(log.total(hwio::device::Channel::Shell)));
    json.insert(QStringLiteral("terminal_buffered"),
                static_cast<qint64>(log.size(hwio::device::Channel::Shell)));
    json.insert(QStringLiteral("log_total"), seqJson(log.total(hwio::device::Channel::Log)));
    json.insert(QStringLiteral("log_buffered"),
                static_cast<qint64>(log.size(hwio::device::Channel::Log)));
    json.insert(QStringLiteral("buffer_size"), static_cast<qint64>(log.capacityPerChannel()));
    std::optional<hwio::device::OperationKind> kind;
    if (!ref.jlink.isNull()) {
        kind = ref.jlink->runningOperationKind();
    }
    json.insert(QStringLiteral("busy_operation"),
                kind ? QJsonValue(qs(hwio::device::operationKindId(*kind)))
                     : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("paused_by_operation"), pausedByOperation(ref));
    return json;
}

QJsonObject deviceStatusSchema() {
    return objectOf({{"id", typed("string")},
                     {"connection", connectionSchema()},
                     {"terminal_total", typed("integer")},
                     {"terminal_buffered", typed("integer")},
                     {"log_total", typed("integer")},
                     {"log_buffered", typed("integer")},
                     {"buffer_size", typed("integer")},
                     {"busy_operation", nullable("string")},
                     {"paused_by_operation", typed("boolean")}},
                    {QStringLiteral("id")});
}

QJsonObject devicesJson(IDeviceProvider& provider) {
    QJsonArray devices;
    for (const DeviceEntry& entry : provider.list()) {
        devices.append(deviceEntryJson(entry));
    }
    QJsonObject json;
    json.insert(QStringLiteral("devices"), devices);
    return json;
}

QJsonObject deviceEntrySchema() {
    return objectOf({{"id", typed("string")},
                     {"kind", typed("string")},
                     {"display_name", typed("string")},
                     {"profile", nullable("string")},
                     {"state", typed("string")},
                     {"owner", nullable("object")},
                     {"connection", nullable("object")},
                     {"opened_by_mcp", typed("boolean")}},
                    {QStringLiteral("id"), QStringLiteral("state")});
}

QJsonObject terminalLineJson(const hwio::device::ConsoleLine& line) {
    QJsonObject json;
    json.insert(QStringLiteral("seq"), seqJson(line.seq));
    json.insert(QStringLiteral("dir"), qs(hwio::device::directionId(line.dir)));
    json.insert(QStringLiteral("text"), qs(line.text));
    json.insert(QStringLiteral("time"), isoTime(line.time));
    return json;
}

QJsonObject logLineJson(const hwio::device::ConsoleLine& line) {
    QJsonObject json;
    json.insert(QStringLiteral("seq"), seqJson(line.seq));
    const auto level = hwio::device::levelId(line.level);
    json.insert(QStringLiteral("level"),
                level ? QJsonValue(qs(*level)) : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("text"), qs(line.text));
    json.insert(QStringLiteral("time"), isoTime(line.time));
    return json;
}

QJsonArray hexdump(std::uint32_t address, std::span<const std::byte> bytes) {
    QJsonArray lines;
    constexpr std::size_t perLine = 16;
    for (std::size_t offset = 0; offset < bytes.size(); offset += perLine) {
        const auto chunk = bytes.subspan(offset, std::min(perLine, bytes.size() - offset));
        QString hex;
        QString ascii;
        for (const std::byte b : chunk) {
            const auto value = std::to_integer<unsigned>(b);
            if (!hex.isEmpty()) {
                hex += QLatin1Char(' ');
            }
            hex += QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0'));
            ascii +=
                value >= 32 && value < 127 ? QChar(static_cast<char16_t>(value)) : QLatin1Char('.');
        }
        // R13: one multi-argument arg() (a chained arg() would expand "%n" inside a value).
        lines.append(QStringLiteral("%1: %2 |%3|")
                         .arg(formatAddress(address + static_cast<std::uint32_t>(offset)),
                              hex.leftJustified(47), ascii));
    }
    return lines;
}

QString hexString(std::span<const std::byte> bytes) {
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        const auto value = std::to_integer<unsigned>(b);
        text += digits[value >> 4U];
        text += digits[value & 0x0FU];
    }
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

std::optional<std::vector<std::byte>> decodeHex(const QString& text) {
    std::vector<std::byte> bytes;
    int high = -1;
    for (const QChar c : text) {
        if (c.isSpace()) {
            continue;
        }
        int value = -1;
        const char16_t u = c.unicode();
        if (u >= u'0' && u <= u'9') {
            value = u - u'0';
        } else if (u >= u'a' && u <= u'f') {
            value = u - u'a' + 10;
        } else if (u >= u'A' && u <= u'F') {
            value = u - u'A' + 10;
        } else {
            return std::nullopt;
        }
        if (high < 0) {
            high = value;
        } else {
            bytes.push_back(static_cast<std::byte>((high << 4) | value));
            high = -1;
        }
    }
    if (high >= 0) {
        return std::nullopt;
    }
    return bytes;
}

// ---- JSON Schema helpers ------------------------------------------------------------------------

QJsonObject typed(const char* type, const char* description) {
    QJsonObject json;
    json.insert(QStringLiteral("type"), QString::fromLatin1(type));
    if (description != nullptr) {
        json.insert(QStringLiteral("description"), QString::fromLatin1(description));
    }
    return json;
}

QJsonObject nullable(const char* type) {
    QJsonObject json;
    json.insert(QStringLiteral("type"),
                QJsonArray{QString::fromLatin1(type), QStringLiteral("null")});
    return json;
}

QJsonObject arrayOf(const QJsonObject& items) {
    QJsonObject json = typed("array");
    json.insert(QStringLiteral("items"), items);
    return json;
}

QJsonObject objectOf(std::initializer_list<std::pair<const char*, QJsonObject>> props,
                     const QStringList& required) {
    QJsonObject properties;
    for (const auto& [name, schema] : props) {
        properties.insert(QString::fromLatin1(name), schema);
    }
    QJsonObject json = typed("object");
    json.insert(QStringLiteral("properties"), properties);
    if (!required.isEmpty()) {
        json.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    }
    return json;
}

QJsonObject connectionSchema() {
    return objectOf({{"state", typed("string")},
                     {"error", nullable("object")},
                     {"target_mv", nullable("integer")}},
                    {QStringLiteral("state")});
}

QJsonObject withNumberEnum(QJsonObject schema, const QString& name, const QJsonArray& values) {
    QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    QJsonObject property = properties.value(name).toObject();
    property.insert(QStringLiteral("enum"), values);
    properties.insert(name, property);
    schema.insert(QStringLiteral("properties"), properties);
    return schema;
}

// ---- argument helpers ---------------------------------------------------------------------------

hwio::mcp::Schema deviceArgument() {
    return hwio::mcp::Schema::string(
               QStringLiteral("Device id from list_devices (\"jlink:<serial>\" or \"ble:<id>\"). "
                              "May be left out when exactly one device is connected."))
        .minLength(1)
        .maxLength(160);
}

hwio::mcp::Schema targetArgument() {
    return hwio::mcp::Schema::string(
               QStringLiteral("MCU of the device (CHESTER: \"app\" or \"lte\"). Default: the "
                              "target the console is connected to."))
        .minLength(1)
        .maxLength(32);
}

std::optional<hwio::mcp::ValidationError> invalid(const QString& name, const QString& message) {
    return hwio::mcp::ValidationError{.pointer = QStringLiteral("/arguments/") + name,
                                      .message = message};
}

hwio::Error deviceNotOpen(const std::string& id) {
    return makeError(ErrorCode::McpDeviceNotOpen, id);
}

hwio::Error capabilityMissing(const std::string& id) {
    return makeError(ErrorCode::McpCapabilityMissing, id);
}

hwio::Error invalidArgument(const QString& message, std::string detail) {
    hwio::Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = utf8(message);
    return error;
}

hwio::Error busyError(hwio::device::OperationKind kind) {
    return makeError(ErrorCode::InternalBusy, std::string(hwio::device::operationKindId(kind)));
}

bool pausedByOperation(const DeviceRef& ref) {
    if (ref.jlink.isNull()) {
        return false;
    }
    const auto kind = ref.jlink->runningOperationKind();
    return kind.has_value() && !hwio::device::isLightOperation(*kind);
}

hwio::Result<ResolvedDevice> resolveDevice(IDeviceProvider& provider,
                                           const QJsonObject& arguments) {
    std::string id = arguments.value(QStringLiteral("device")).toString().toStdString();
    if (id.empty()) {
        std::vector<std::string> open;
        for (const DeviceEntry& entry : provider.list()) {
            if (entry.state == DeviceEntry::State::Open) {
                open.push_back(entry.id);
            }
        }
        if (open.empty()) {
            return std::unexpected(deviceNotOpen({}));
        }
        if (open.size() > 1) {
            std::string ids;
            for (const std::string& item : open) {
                ids += ids.empty() ? item : ", " + item;
            }
            return std::unexpected(makeError(ErrorCode::McpDeviceAmbiguous, ids));
        }
        id = open.front();
    }
    auto ref = provider.find(id);
    if (!ref || !ref->alive()) {
        return std::unexpected(deviceNotOpen(id));
    }
    return ResolvedDevice{.id = std::move(id), .ref = *ref};
}

hwio::Result<ResolvedDevice> resolveJLinkDevice(IDeviceProvider& provider,
                                                const QJsonObject& arguments) {
    auto device = resolveDevice(provider, arguments);
    if (!device) {
        return device;
    }
    if (device->ref.jlink.isNull()) {
        return std::unexpected(capabilityMissing(device->id));
    }
    return device;
}

hwio::Result<const hwio::device::TargetProfile*>
resolveTarget(const hwio::device::ConsoleSession& session, const QJsonObject& arguments) {
    const QString requested = arguments.value(QStringLiteral("target")).toString();
    const std::string id =
        requested.isEmpty() ? std::string(session.consoleTargetId()) : requested.toStdString();
    const auto* target = hwio::device::findTarget(session.profile(), id);
    if (target == nullptr) {
        QStringList known;
        for (const auto& item : session.profile().targets) {
            known.append(qs(item.id));
        }
        return std::unexpected(
            invalidArgument(QStringLiteral("The device has no target %1.").arg(qs(id)),
                            "targets: " + known.join(QStringLiteral(", ")).toStdString()));
    }
    return target;
}

hwio::Result<fs::path> inputFile(const QString& path, std::uint64_t maxBytes) {
    fs::path file(path.toStdU16String());
    if (!file.is_absolute()) {
        return std::unexpected(invalidArgument(
            QStringLiteral("The path %1 is not absolute.").arg(path), path.toStdString()));
    }
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return std::unexpected(makeError(ErrorCode::IoFileNotFound, path.toStdString()));
    }
    if (!fs::is_regular_file(file, ec)) {
        return std::unexpected(invalidArgument(
            QStringLiteral("The path %1 is not a regular file.").arg(path), path.toStdString()));
    }
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec || size > maxBytes) {
        return std::unexpected(invalidArgument(tooLarge(path, maxBytes), path.toStdString()));
    }
    return file;
}

hwio::Result<std::vector<std::byte>> readInputFile(const fs::path& path, std::uint64_t maxBytes) {
    QFile file(QString::fromStdU16String(path.u16string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(ErrorCode::IoReadFailed,
                                         hwio::toUtf8(path) + ": " + utf8(file.errorString())));
    }
    const QByteArray data = file.read(static_cast<qint64>(maxBytes) + 1);
    if (std::cmp_greater(data.size(), maxBytes)) {
        return std::unexpected(invalidArgument(
            tooLarge(QString::fromStdU16String(path.u16string()), maxBytes), hwio::toUtf8(path)));
    }
    const auto view =
        std::as_bytes(std::span(data.constData(), static_cast<std::size_t>(data.size())));
    return std::vector<std::byte>(view.begin(), view.end());
}

// ---- markers ------------------------------------------------------------------------------------

// Markers and busy texts are shown in the GUI console (plan 0014) and stay translatable (ADR-0103,
// review R12). QCoreApplication::translate() is called with literal arguments at every site so
// that lupdate extracts them with the context "hwio::mcp" (a tr() wrapper would be extracted with
// the enclosing namespace as context and never found at run time).

QString markerStart(const hwio::mcp::RequestContext& context, const QString& action) {
    const QString client =
        context.client.name.empty() ? QStringLiteral("?") : qs(context.client.name);
    return QCoreApplication::translate("hwio::mcp", "— AI (%1): %2 —").arg(client, action);
}

QString markerDone(std::string_view tool, std::chrono::milliseconds duration) {
    return QCoreApplication::translate("hwio::mcp", "— AI: %1 done in %2 s —")
        .arg(qs(tool), QString::number(seconds(duration), 'f', 1));
}

QString markerFailed(std::string_view tool, const hwio::Error& error) {
    return QCoreApplication::translate("hwio::mcp", "— AI: %1 failed: %2 —")
        .arg(qs(tool), qs(error.message));
}

QString markerCancelled(std::string_view tool) {
    return QCoreApplication::translate("hwio::mcp", "— AI: %1 cancelled —").arg(qs(tool));
}

QString busyText(std::string_view tool) {
    if (tool == "flash" || tool == "flash_lte" || tool == "write_flash") {
        return QCoreApplication::translate("hwio::mcp", "AI: Flashing…");
    }
    if (tool == "erase") {
        return QCoreApplication::translate("hwio::mcp", "AI: Erasing…");
    }
    if (tool == "recover") {
        return QCoreApplication::translate("hwio::mcp", "AI: Recovering…");
    }
    if (tool == "write_memory") {
        return QCoreApplication::translate("hwio::mcp", "AI: Writing memory…");
    }
    if (tool == "reset") {
        return QCoreApplication::translate("hwio::mcp", "AI: Resetting…");
    }
    return QCoreApplication::translate("hwio::mcp", "AI: %1…").arg(qs(tool));
}

// ---- registration -------------------------------------------------------------------------------

hwio::Status addTool(hwio::mcp::Registry& registry, hwio::mcp::ToolSpec spec) {
    return registry.addTool(std::move(spec));
}

std::unique_ptr<hwio::mcp::ToolCall> done(hwio::Result<QJsonObject> result) {
    if (!result) {
        return hwio::mcp::ToolCall::completed(std::unexpected(std::move(result).error()));
    }
    return hwio::mcp::ToolCall::completed(hwio::mcp::toolResult(*result));
}

std::unique_ptr<hwio::mcp::ToolCall> failed(hwio::Error error) {
    return hwio::mcp::ToolCall::completed(std::unexpected(std::move(error)));
}

// ---- DeviceCall ---------------------------------------------------------------------------------

DeviceCall::DeviceCall(ToolsContext& tools, hwio::mcp::RequestContext context,
                       const hwio::mcp::ToolEnv& env, ResolvedDevice device)
    : m_tools(&tools), m_consent(&env.consentBroker()), m_context(std::move(context)),
      m_limits(env.limits().tools), m_id(std::move(device.id)), m_ref(std::move(device.ref)) {
    setDeviceId(m_id);
    if (!m_ref.console.isNull()) {
        connect(m_ref.console.data(), &QObject::destroyed, this, [this] {
            if (isFinished()) {
                return;
            }
            deviceGone();
            completeError(deviceNotOpen(m_id));
        });
    }
}

void DeviceCall::complete(hwio::Result<QJsonObject> result) {
    if (isFinished()) {
        return;
    }
    if (!result) {
        finish(std::unexpected(std::move(result).error()));
        return;
    }
    finish(hwio::mcp::toolResult(*result));
}

void DeviceCall::completeError(hwio::Error error) { complete(std::unexpected(std::move(error))); }

// ---- OperationCall ------------------------------------------------------------------------------

OperationCall::OperationCall(ToolsContext& tools, const hwio::mcp::RequestContext& context,
                             const hwio::mcp::ToolEnv& env, ResolvedDevice device,
                             OperationSettings settings)
    : DeviceCall(tools, context, env, std::move(device)), m_settings(settings) {
    m_stall.setSingleShot(true);
    m_stall.setInterval(this->limits().operationStall);
    connect(&m_stall, &QTimer::timeout, this, [this] { stalled(); });
    if (auto* jlink = session(); jlink != nullptr) {
        connect(jlink, &hwio::device::DeviceSession::operationProgress, this,
                [this](hwio::device::OperationId id, const hwio::device::OperationProgress& p) {
                    onProgress(id, p);
                });
        connect(jlink, &hwio::device::DeviceSession::operationFinished, this,
                [this](hwio::device::OperationId id, const hwio::device::OperationResult& r) {
                    onFinished(id, r);
                });
    }
}

OperationCall::~OperationCall() { endBusy(); }

void OperationCall::withConsent(hwio::mcp::ConsentCategories categories, QJsonObject details,
                                std::function<void()> next) {
    if (categories == hwio::mcp::ConsentCategories()) {
        next();
        return;
    }
    requestConsent(consentBroker(),
                   hwio::mcp::makeConsentRequest(context(), categories, id(), std::move(details)),
                   [this, next = std::move(next)](const hwio::mcp::ConsentDecision& decision) {
                       if (!decision) {
                           completeError(decision.error());
                           return;
                       }
                       next();
                   });
}

bool OperationCall::begin(const QString& action) {
    if (isFinished()) {
        return false;
    }
    if (!alive() || session() == nullptr) {
        completeError(deviceNotOpen(id()));
        return false;
    }
    if (const auto kind = session()->runningOperationKind()) {
        completeError(busyError(*kind));
        return false;
    }
    m_begun = std::chrono::steady_clock::now();
    if (m_settings.marker) {
        provider().marker(id(), markerStart(context(), action));
        m_begunMarker = true;
    }
    if (m_settings.busy) {
        m_busy = true;
        const QPointer<OperationCall> self(this);
        provider().beginBusy(id(), busyText(m_settings.tool), [self] {
            if (!self.isNull()) {
                self->cancel();
            }
        });
    }
    return true;
}

void OperationCall::run(const OperationStart& start,
                        std::function<void(const hwio::device::OperationResult&)> then) {
    if (isFinished()) {
        return;
    }
    if (!alive() || session() == nullptr) {
        failWith(deviceNotOpen(id()));
        return;
    }
    if (isCancelled()) {
        failWith(makeError(ErrorCode::Cancelled));
        return;
    }
    auto operation = start(*session());
    if (!operation) {
        if (operation.error().code == ErrorCode::InternalBusy) {
            if (const auto kind = session()->runningOperationKind()) {
                failWith(busyError(*kind));
                return;
            }
        }
        failWith(std::move(operation).error());
        return;
    }
    m_operation = *operation;
    m_then = std::move(then);
    m_stall.start();
}

void OperationCall::onProgress(hwio::device::OperationId id,
                               const hwio::device::OperationProgress& progress) {
    if (!m_operation || *m_operation != id || isFinished()) {
        return;
    }
    m_stall.start();
    if (!m_settings.progress) {
        return;
    }
    const double steps = std::max(1, progress.steps);
    const double fraction = progress.total > 0
                                ? static_cast<double>(std::min(progress.done, progress.total)) /
                                      static_cast<double>(progress.total)
                                : 0.0;
    reportProgress((static_cast<double>(std::max(1, progress.step)) - 1.0) + fraction, steps,
                   qs(hwio::firmware::phaseId(progress.phase)));
}

void OperationCall::onFinished(hwio::device::OperationId id,
                               const hwio::device::OperationResult& result) {
    if (!m_operation || *m_operation != id || isFinished()) {
        return;
    }
    m_operation.reset();
    m_stall.stop();
    if (!result.status) {
        // A failure after a cancel request is the cancellation's doing.
        failWith(m_cancelling ? makeError(ErrorCode::Cancelled) : result.status.error());
        return;
    }
    // Review R14: the operation completed before the cancel took effect - report the real
    // result. A multi-step call stops at its next run() (isCancelled()).
    auto then = std::exchange(m_then, {});
    if (then) {
        then(result);
    }
}

void OperationCall::stalled() {
    if (isFinished()) {
        return;
    }
    if (m_operation && session() != nullptr) {
        if (const Status cancelled = session()->cancel(*m_operation); !cancelled) {
            qCWarning(hwio::log::lcMcp).noquote()
                << "MCP: cancelling a stalled operation failed:" << qs(cancelled.error().message);
        }
    }
    m_operation.reset();
    qCWarning(hwio::log::lcMcp).noquote()
        << "MCP:" << qs(m_settings.tool) << "on" << qs(id()) << "stalled; operation cancelled";
    hwio::Error error = makeError(
        ErrorCode::Timeout,
        std::format(
            "no operation progress for {} s",
            std::chrono::duration_cast<std::chrono::seconds>(limits().operationStall).count()));
    failWith(std::move(error));
}

void OperationCall::succeed(QJsonObject structured, std::chrono::milliseconds duration) {
    if (isFinished()) {
        return;
    }
    if (m_begunMarker && m_settings.busy && alive()) {
        provider().marker(id(), markerDone(m_settings.tool, duration));
    }
    if (m_settings.cursors && alive()) {
        const std::uint64_t cursor = ref().console->log().lastSeq();
        structured.insert(QStringLiteral("log_cursor_end"), seqJson(cursor));
        structured.insert(QStringLiteral("terminal_cursor_end"), seqJson(cursor));
    }
    endBusy();
    complete(std::move(structured));
}

void OperationCall::failWith(hwio::Error error) {
    if (isFinished()) {
        return;
    }
    m_stall.stop();
    if (m_begunMarker && m_settings.busy && alive()) {
        provider().marker(id(), error.code == ErrorCode::Cancelled
                                    ? markerCancelled(m_settings.tool)
                                    : markerFailed(m_settings.tool, error));
    }
    endBusy();
    completeError(std::move(error));
}

std::chrono::milliseconds OperationCall::sinceBegin() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 m_begun);
}

void OperationCall::onCancel() {
    if (m_operation && session() != nullptr) {
        m_cancelling = true;
        if (Status cancelled = session()->cancel(*m_operation); cancelled) {
            return; // operationFinished follows
        }
    }
    cancelPending();
    failWith(makeError(ErrorCode::Cancelled));
}

void OperationCall::deviceGone() {
    m_stall.stop();
    m_operation.reset();
    endBusy();
}

void OperationCall::endBusy() {
    if (m_busy) {
        m_busy = false;
        provider().endBusy(id());
    }
}

// ---- SimpleOperationCall ------------------------------------------------------------------------

SimpleOperationCall::SimpleOperationCall(ToolsContext& tools,
                                         const hwio::mcp::RequestContext& context,
                                         const hwio::mcp::ToolEnv& env, ResolvedDevice device,
                                         SimpleOperation op)
    : OperationCall(tools, context, env, std::move(device), op.settings), m_op(std::move(op)) {
    withConsent(m_op.consent, m_op.details, [this] {
        if (!begin(m_op.action)) {
            return;
        }
        run(m_op.start, [this](const hwio::device::OperationResult& result) {
            auto json = m_op.result(result);
            if (!json) {
                failWith(std::move(json).error());
                return;
            }
            succeed(std::move(*json), result.duration);
        });
    });
}

hwio::mcp::ToolStart operationStart(ToolsContext& context, OperationBuilder builder,
                                    std::string_view fixedTarget) {
    return [&context, builder = std::move(builder),
            fixedTarget](const hwio::mcp::RequestContext& rc, const QJsonObject& args,
                         hwio::mcp::ToolEnv& env) -> std::unique_ptr<hwio::mcp::ToolCall> {
        auto device = resolveJLinkDevice(context.provider(), args);
        if (!device) {
            return failed(std::move(device).error());
        }
        QJsonObject targetArgs = args;
        if (!fixedTarget.empty()) {
            targetArgs.insert(QStringLiteral("target"), qs(fixedTarget));
        }
        auto target = resolveTarget(*device->ref.console, targetArgs);
        if (!target) {
            return failed(std::move(target).error());
        }
        auto op = builder(args, *device, **target, env.limits().tools);
        if (!op) {
            return failed(std::move(op).error());
        }
        return std::make_unique<SimpleOperationCall>(context, rc, env, std::move(*device),
                                                     std::move(*op));
    };
}

} // namespace hwio::mcp::tools::detail

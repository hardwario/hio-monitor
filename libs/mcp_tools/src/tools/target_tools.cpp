// Target tools (plan/features/0013 C2, ADR-0099, ADR-0100, ADR-0157): target_status, reset, halt,
// go, read_memory, read_registers, memory_zones, read_pib, read_uicr, write_memory, write_flash.
// All run as DeviceSession operations (one at a time; internal.busy with the running kind).

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/pib.h"
#include "hio/device/profile.h"
#include "hio/firmware/firmware_image.h"
#include "hio/hw/jlink/programmer.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"
#include "hio/mcp/tools/tools_context.h"
#include "tool_support.h"
#include "tools/tools.h"

#include <QChar>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::mcp::tools::detail {

namespace {

namespace fs = std::filesystem;
using hio::device::AccessWidth;
using hio::device::DeviceSession;
using hio::device::OperationResult;
using hio::mcp::Required;
using hio::mcp::Schema;

constexpr const char* targetStatusDescription =
    "Whether the target CPU is halted, its core id and the J-Link device name. J-Link only.";

constexpr const char* resetDescription =
    "Reset the target MCU. halt=false: the firmware restarts and the console re-attaches by "
    "itself (use wait_for_connection, then read_log with after_cursor=log_cursor_end of this "
    "result... or the boot banner). halt=true: the CPU stays halted at the reset vector; call go "
    "to run it. Takes about 1 s. J-Link only.";

constexpr const char* haltDescription =
    "Halt the target CPU without a reset. The RTT console keeps working (RAM stays readable) but "
    "the firmware stops until go or reset. J-Link only.";

constexpr const char* goDescription = "Resume a halted target CPU. J-Link only.";

constexpr const char* readMemoryDescription =
    "Read target memory (flash, UICR, RAM, peripherals) through J-Link while the firmware runs. "
    "Flash is memory-mapped, so this also reads flash. address: \"0x20000000\" or decimal. "
    "width: access width in bits (8, 16 or 32; use 32 for peripheral registers); the address and "
    "length must be multiples of width/8. Inline results have at most 65536 bytes (hex + "
    "hexdump). to_file writes the raw bytes (up to 16 MiB) to a file on this machine instead: an "
    "absolute path in an existing folder, or a bare file name (no '/', '\\' or ':', not a "
    "Windows device name such as NUL or COM1, no trailing dot or space) that goes into the "
    "server's cache folder; existing files are never overwritten (io.file_exists). The "
    "result then has the path, the sha256 and a preview. J-Link only.";

constexpr const char* readRegistersDescription =
    "Read the core registers (R0-R15, xPSR, MSP, PSP, ...) as hex strings. The CPU must be "
    "halted first (halt), else target.not_halted. PC/LR/SP show where the firmware is. J-Link "
    "only.";

constexpr const char* memoryZonesDescription =
    "List the memory zones the J-Link library reports for the target (often none). J-Link only.";

constexpr const char* readPibDescription =
    "Read and decode the HARDWARIO Product Information Block of the application MCU (vendor, "
    "product, hardware variant and revision, serial number, claim token, BLE passkey); the same "
    "JSON as `hio chester app pib read --json`. J-Link only.";

constexpr const char* readUicrDescription =
    "Read the 128-byte UICR customer area (the PIB) of the application MCU as hex; the same JSON "
    "as `hio chester app uicr read --format hex - --json`. J-Link only.";

constexpr const char* writeMemoryDescription =
    "Write target RAM or peripheral registers through J-Link (not flash: use write_flash). "
    "address: \"0x20000000\" or decimal. Data as data_hex (e.g. \"deadbeef\", units "
    "little-endian, at most 64 KiB) or path (an absolute path to a raw binary file on this "
    "machine, at most 64 KiB), exactly one of them. width: 8, 16 or 32 bits per access; address "
    "and size must be multiples of width/8. Only the RAM and peripheral ranges of the target are "
    "allowed, without the flash controller, the System Control Space and the FPB "
    "(target.address_out_of_range). Writing memory can make the device run arbitrary code, which "
    "can also erase its flash and the PIB. Needs the user's permission (memory-write). J-Link "
    "only.";

constexpr const char* writeFlashDescription =
    "Program raw bytes into internal flash: reset and halt, erase and program (the rest of each "
    "touched page is kept), verify, reset; the firmware reboots and the console re-attaches "
    "(about 2-10 s). Data as data_hex (at most 1 MiB) or path (absolute path to a raw binary "
    "file, at most 1 MiB), exactly one. Writing into the UICR needs allow_uicr=true (else "
    "firmware.uicr_in_image); the Product Information Block is never written. For whole .hex "
    "images use flash. Needs the user's permission (flash; also uicr with allow_uicr when bytes "
    "go into the UICR). Afterwards read only new output with read_log "
    "after_cursor=log_cursor_end of the result. J-Link only.";

[[nodiscard]] AccessWidth widthOf(const QJsonObject& args) {
    switch (args.value(QStringLiteral("width")).toInt(8)) {
    case 16:
        return AccessWidth::Bits16;
    case 32:
        return AccessWidth::Bits32;
    default:
        return AccessWidth::Bits8;
    }
}

// Review F5: on every OS, a bare to_file name must not be a Windows device name (CON, PRN, AUX,
// NUL, COM1-COM9, LPT1-LPT9; case-insensitive, also with an extension or with spaces before it,
// e.g. "nul.txt", "COM1.bin") and must not end with a dot or a space (Windows drops them).
[[nodiscard]] bool isWindowsReservedName(const QString& name) {
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        return true;
    }
    QString stem = name.section(QLatin1Char('.'), 0, 0);
    while (stem.endsWith(QLatin1Char(' '))) {
        stem.chop(1);
    }
    stem = stem.toUpper();
    if (stem == QStringLiteral("CON") || stem == QStringLiteral("PRN") ||
        stem == QStringLiteral("AUX") || stem == QStringLiteral("NUL")) {
        return true;
    }
    constexpr qsizetype numberedLength = 4; // "COM1"
    return stem.size() == numberedLength &&
           (stem.startsWith(QStringLiteral("COM")) || stem.startsWith(QStringLiteral("LPT"))) &&
           stem.at(3) >= QLatin1Char('1') && stem.at(3) <= QLatin1Char('9');
}

[[nodiscard]] QString rangeText(std::uint32_t address, std::size_t size) {
    return QStringLiteral("%1-%2").arg(
        formatAddress(address),
        formatAddress(static_cast<std::uint32_t>(address + std::max<std::size_t>(size, 1) - 1)));
}

hio::Result<QJsonObject> pibJson(std::span<const std::byte> data) {
    if (data.size() != hio::device::pibSize) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "short PIB read"));
    }
    auto pib = hio::device::decodePib(data.first<hio::device::pibSize>());
    if (!pib) {
        return std::unexpected(std::move(pib).error());
    }
    return hio::device::pibToJson(*pib);
}

// As `hio chester app uicr read --format hex - --json`.
hio::Result<QJsonObject> uicrJson(std::span<const std::byte> data) {
    QJsonObject json;
    json.insert(QStringLiteral("format"), QStringLiteral("hex"));
    json.insert(QStringLiteral("file"), QStringLiteral("-"));
    json.insert(QStringLiteral("bytes"), static_cast<double>(data.size()));
    json.insert(QStringLiteral("hex"), hexString(data));
    return json;
}

[[nodiscard]] QString formatAddress64(std::uint64_t address) {
    if (address <= 0xFFFFFFFFULL) {
        return formatAddress(static_cast<std::uint32_t>(address));
    }
    return QStringLiteral("0x") +
           QStringLiteral("%1")
               .arg(static_cast<qulonglong>(address), 16, 16, QLatin1Char('0'))
               .toUpper();
}

// data_hex / path of write_memory and write_flash.
[[nodiscard]] hio::Result<std::vector<std::byte>> dataArgument(const QJsonObject& args,
                                                               std::size_t maxBytes) {
    if (args.contains(QStringLiteral("data_hex"))) {
        auto bytes = decodeHex(args.value(QStringLiteral("data_hex")).toString());
        if (!bytes) {
            return std::unexpected(
                invalidArgument(QStringLiteral("data_hex is not valid hex."), {}));
        }
        return std::move(*bytes);
    }
    auto file = inputFile(args.value(QStringLiteral("path")).toString(), maxBytes);
    if (!file) {
        return std::unexpected(std::move(file).error());
    }
    return readInputFile(*file, maxBytes);
}

std::optional<hio::mcp::ValidationError> checkData(const QJsonObject& args, std::size_t maxBytes) {
    const bool hex = args.contains(QStringLiteral("data_hex"));
    const bool path = args.contains(QStringLiteral("path"));
    if (hex == path) {
        return invalid(hex ? QStringLiteral("path") : QStringLiteral("data_hex"),
                       QStringLiteral("exactly one of data_hex and path is required"));
    }
    if (hex) {
        const auto bytes = decodeHex(args.value(QStringLiteral("data_hex")).toString());
        if (!bytes) {
            return invalid(QStringLiteral("data_hex"),
                           QStringLiteral("must be an even number of hex digits"));
        }
        if (bytes->empty() || bytes->size() > maxBytes) {
            return invalid(QStringLiteral("data_hex"),
                           QStringLiteral("must hold 1 to %1 bytes").arg(maxBytes));
        }
    }
    return std::nullopt;
}

QJsonObject addressedSchema(std::initializer_list<std::pair<const char*, QJsonObject>> extra) {
    QJsonObject schema = objectOf({{"address", typed("string")}}, {QStringLiteral("address")});
    QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    for (const auto& [name, value] : extra) {
        properties.insert(QString::fromLatin1(name), value);
    }
    schema.insert(QStringLiteral("properties"), properties);
    return schema;
}

// ---- read_memory to_file ------------------------------------------------------------------------

// Reads `length` bytes in chunks of readMemoryInlineBytes into a new file (never overwrites,
// ADR-0100); the partial file is removed after a failure.
class ReadMemoryFileCall final : public OperationCall {
public:
    ReadMemoryFileCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                       const hio::mcp::ToolEnv& env, ResolvedDevice device, std::string targetId,
                       std::uint32_t address, std::size_t length, AccessWidth width, fs::path file)
        : OperationCall(tools, context, env, std::move(device), readSettings("read_memory")),
          m_target(std::move(targetId)), m_address(address), m_length(length), m_width(width),
          m_path(std::move(file)), m_hash(QCryptographicHash::Sha256) {
        const QString path = QString::fromStdU16String(m_path.u16string());
        std::error_code ec;
        // Review R15: missing folders are never created (the cache folder of a bare name is
        // created by the tool before this call).
        if (const fs::path parent = m_path.parent_path(); !fs::is_directory(parent, ec)) {
            hio::Error error =
                makeError(ErrorCode::IoWriteFailed, path.toStdString() + ": the folder " +
                                                        hio::toUtf8(parent) + " does not exist");
            error.cause = "The folder of the file does not exist; it is not created.";
            error.suggestion = "Create the folder first, or use another path or a bare file name.";
            completeError(std::move(error));
            return;
        }
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            const bool exists = fs::exists(m_path, ec);
            completeError(makeError(
                exists ? ErrorCode::IoFileExists : ErrorCode::IoWriteFailed,
                path.toStdString() +
                    (exists ? std::string() : ": " + m_file.errorString().toStdString())));
            return;
        }
        m_opened = true;
        if (!begin({})) {
            discard();
            return;
        }
        next();
    }

    ~ReadMemoryFileCall() override {
        if (!isFinished() || !m_ok) {
            discard();
        }
    }
    ReadMemoryFileCall(const ReadMemoryFileCall&) = delete;
    ReadMemoryFileCall& operator=(const ReadMemoryFileCall&) = delete;
    ReadMemoryFileCall(ReadMemoryFileCall&&) = delete;
    ReadMemoryFileCall& operator=(ReadMemoryFileCall&&) = delete;

protected:
    void cancelPending() override { discard(); }

private:
    void next() {
        if (m_done >= m_length) {
            finishFile();
            return;
        }
        const std::size_t chunk = std::min(limits().readMemoryInlineBytes, m_length - m_done);
        const auto address = static_cast<std::uint32_t>(m_address + m_done);
        run(
            [this, address, chunk](DeviceSession& session) {
                return session.readMemory(m_target, address, chunk, m_width);
            },
            [this](const OperationResult& result) {
                const auto bytes = std::span<const std::byte>(result.data);
                QByteArray raw(static_cast<qsizetype>(bytes.size()), Qt::Uninitialized);
                std::ranges::transform(bytes, raw.begin(),
                                       [](std::byte b) { return std::to_integer<char>(b); });
                if (m_file.write(raw) != raw.size()) {
                    discard();
                    failWith(makeError(ErrorCode::IoWriteFailed,
                                       m_file.fileName().toStdString() + ": " +
                                           m_file.errorString().toStdString()));
                    return;
                }
                m_hash.addData(raw);
                if (m_preview.empty()) {
                    const auto first = bytes.first(std::min<std::size_t>(64, bytes.size()));
                    m_preview.assign(first.begin(), first.end());
                }
                m_done += bytes.size();
                if (bytes.empty()) {
                    discard();
                    failWith(makeError(ErrorCode::InternalUnexpected, "empty memory read"));
                    return;
                }
                next();
            });
    }

    void finishFile() {
        m_file.close();
        m_ok = true;
        QJsonObject result;
        result.insert(QStringLiteral("address"), formatAddress(m_address));
        result.insert(QStringLiteral("length"), static_cast<qint64>(m_done));
        result.insert(QStringLiteral("file"), m_file.fileName());
        result.insert(QStringLiteral("sha256"), QString::fromLatin1(m_hash.result().toHex()));
        result.insert(QStringLiteral("preview"), hexdump(m_address, m_preview));
        succeed(result, sinceBegin());
    }

    void discard() {
        if (m_file.isOpen()) {
            m_file.close();
        }
        if (!m_ok && m_opened) {
            m_opened = false;
            QFile::remove(m_file.fileName());
        }
    }

    std::string m_target;
    std::uint32_t m_address;
    std::size_t m_length;
    AccessWidth m_width;
    fs::path m_path;
    QFile m_file;
    QCryptographicHash m_hash;
    std::vector<std::byte> m_preview;
    std::size_t m_done = 0;
    bool m_opened = false; // the file was created by this call
    bool m_ok = false;
};

QJsonObject deviceTargetSchema() {
    return Schema::object()
        .property(QStringLiteral("device"), deviceArgument())
        .property(QStringLiteral("target"), targetArgument())
        .json();
}

QJsonObject haltedOutputSchema() {
    return objectOf({{"halted", typed("boolean")}}, {QStringLiteral("halted")});
}

QJsonObject readMemoryInputSchema() {
    return withNumberEnum(
        Schema::object()
            .property(QStringLiteral("device"), deviceArgument())
            .property(QStringLiteral("target"), targetArgument())
            .property(QStringLiteral("address"),
                      Schema::address(QStringLiteral("Start address, \"0x...\" or decimal.")),
                      Required::Yes)
            .property(
                QStringLiteral("length"),
                Schema::integer(QStringLiteral("Bytes to read: 1-65536 inline, up to 16777216 with "
                                               "to_file."))
                    .minimum(1)
                    .maximum(16.0 * 1024 * 1024)
                    .defaultValue(64))
            .property(QStringLiteral("width"),
                      Schema::integer(QStringLiteral("Access width in bits.")).defaultValue(8))
            .property(
                QStringLiteral("to_file"),
                Schema::string(QStringLiteral("Write the bytes to this file instead: an absolute "
                                              "path or a bare file name (server cache folder)."))
                    .minLength(1)
                    .maxLength(1024))
            .json(),
        QStringLiteral("width"), QJsonArray{8, 16, 32});
}

QJsonObject deviceOnlySchema() {
    return Schema::object().property(QStringLiteral("device"), deviceArgument()).json();
}

QJsonObject writeMemoryInputSchema() {
    return withNumberEnum(
        Schema::object()
            .property(QStringLiteral("device"), deviceArgument())
            .property(QStringLiteral("target"), targetArgument())
            .property(QStringLiteral("address"),
                      Schema::address(QStringLiteral("Start address, \"0x...\" or decimal.")),
                      Required::Yes)
            .property(QStringLiteral("data_hex"),
                      Schema::string(QStringLiteral("Bytes as hex digits, e.g. \"deadbeef\"."))
                          .minLength(2)
                          .maxLength((2 * 64 * 1024) + 4096))
            .property(QStringLiteral("path"),
                      Schema::string(QStringLiteral("Absolute path to a raw binary file."))
                          .minLength(1)
                          .maxLength(1024))
            .property(QStringLiteral("width"),
                      Schema::integer(QStringLiteral("Access width in bits.")).defaultValue(8))
            .json(),
        QStringLiteral("width"), QJsonArray{8, 16, 32});
}

hio::Status addTargetStatus(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "target_status",
            .title = "Target status",
            .description = targetStatusDescription,
            .inputSchema = deviceTargetSchema(),
            .outputSchema = objectOf({{"halted", typed("boolean")},
                                      {"core_id", typed("string")},
                                      {"device", typed("string")}},
                                     {QStringLiteral("halted")}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start = operationStart(
                context,
                [](const QJsonObject&, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits&) -> hio::Result<SimpleOperation> {
                    const std::string id(target.id);
                    return SimpleOperation{
                        .settings = readSettings("target_status"),
                        .consent = {},
                        .details = {},
                        .action = {},
                        .start = [id](DeviceSession& s) { return s.targetStatus(id); },
                        .result = [](const OperationResult& r) -> hio::Result<QJsonObject> {
                            const auto status =
                                r.targetStatus.value_or(hio::device::TargetStatus{});
                            QJsonObject json;
                            json.insert(QStringLiteral("halted"), status.halted);
                            if (status.coreId) { // absent without JLINKARM_GetId (S6)
                                json.insert(QStringLiteral("core_id"),
                                            formatAddress(*status.coreId));
                            }
                            json.insert(QStringLiteral("device"), qs(status.deviceName));
                            return json;
                        }};
                }),
            .validate = {}});
}

hio::Status addReset(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "reset",
            .title = "Reset the target",
            .description = resetDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("target"), targetArgument())
                    .property(QStringLiteral("halt"),
                              Schema::boolean(QStringLiteral("Stay halted at the reset vector."))
                                  .defaultValue(false))
                    .json(),
            .outputSchema = objectOf({{"halted", typed("boolean")},
                                      {"log_cursor_end", typed("integer")},
                                      {"terminal_cursor_end", typed("integer")}},
                                     {QStringLiteral("halted")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = operationStart(
                context,
                [](const QJsonObject& args, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits&) -> hio::Result<SimpleOperation> {
                    const std::string id(target.id);
                    const bool halt = args.value(QStringLiteral("halt")).toBool();
                    return SimpleOperation{
                        .settings = OperationSettings{.tool = "reset",
                                                      .marker = true,
                                                      .busy = true,
                                                      .cursors = true,
                                                      .progress = false},
                        .consent = {},
                        .details = {},
                        .action = halt ? QStringLiteral("reset %1 (halt)").arg(qs(id))
                                       : QStringLiteral("reset %1").arg(qs(id)),
                        .start =
                            [id, halt](DeviceSession& s) {
                                return s.reset(id, halt ? hio::device::ResetMode::Halt
                                                        : hio::device::ResetMode::Go);
                            },
                        .result = [halt](const OperationResult&) -> hio::Result<QJsonObject> {
                            QJsonObject json;
                            json.insert(QStringLiteral("halted"), halt);
                            return json;
                        }};
                }),
            .validate = {}});
}

hio::Status addHaltAndGo(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const bool haltTool : {true, false}) {
        const std::string_view name = haltTool ? "halt" : "go";
        if (auto added = addTool(
                registry,
                hio::mcp::ToolSpec{
                    .name = name,
                    .title = haltTool ? "Halt the CPU" : "Resume the CPU",
                    .description = haltTool ? haltDescription : goDescription,
                    .inputSchema = deviceTargetSchema(),
                    .outputSchema = haltedOutputSchema(),
                    .annotations = control,
                    .consent = {},
                    .marker = true,
                    .start = operationStart(
                        context,
                        [haltTool,
                         name](const QJsonObject&, const ResolvedDevice&,
                               const hio::device::TargetProfile& target,
                               const hio::mcp::ToolLimits&) -> hio::Result<SimpleOperation> {
                            const std::string id(target.id);
                            return SimpleOperation{
                                .settings = OperationSettings{.tool = name,
                                                              .marker = true,
                                                              .busy = false,
                                                              .cursors = false,
                                                              .progress = false},
                                .consent = {},
                                .details = {},
                                .action = QStringLiteral("%1 %2").arg(qs(name), qs(id)),
                                .start =
                                    [id, haltTool](DeviceSession& s) {
                                        return haltTool ? s.halt(id) : s.go(id);
                                    },
                                .result = [haltTool](const OperationResult& r)
                                    -> hio::Result<QJsonObject> {
                                    QJsonObject json;
                                    json.insert(QStringLiteral("halted"),
                                                r.targetStatus ? r.targetStatus->halted : haltTool);
                                    return json;
                                }};
                        }),
                    .validate = {}});
            !added) {
            return added;
        }
    }
    return {};
}

hio::Status addReadMemory(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "read_memory",
            .title = "Read memory",
            .description = readMemoryDescription,
            .inputSchema = readMemoryInputSchema(),
            .outputSchema = addressedSchema({{"length", typed("integer")},
                                             {"hex", typed("string")},
                                             {"dump", arrayOf(typed("string"))},
                                             {"file", typed("string")},
                                             {"sha256", typed("string")},
                                             {"preview", arrayOf(typed("string"))}}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start = [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                hio::mcp::ToolEnv& env) -> std::unique_ptr<hio::mcp::ToolCall> {
                auto device = resolveJLinkDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                auto target = resolveTarget(*device->ref.console, args);
                if (!target) {
                    return failed(std::move(target).error());
                }
                const std::string targetId((*target)->id);
                const auto address =
                    parseAddress(args.value(QStringLiteral("address")).toString()).value_or(0);
                const auto length =
                    static_cast<std::size_t>(args.value(QStringLiteral("length")).toInteger());
                const AccessWidth width = widthOf(args);
                if (args.contains(QStringLiteral("to_file"))) {
                    const QString name = args.value(QStringLiteral("to_file")).toString();
                    fs::path file(name.toStdU16String());
                    if (!file.is_absolute()) {
                        // A bare name (validated): the server's own cache folder, created on
                        // first use (ADR-0100).
                        const fs::path directory = context.memoryDumpDirectory();
                        std::error_code ec;
                        fs::create_directories(directory, ec);
                        file = directory / file;
                    }
                    return std::make_unique<ReadMemoryFileCall>(context, rc, env,
                                                                std::move(*device), targetId,
                                                                address, length, width, file);
                }
                return std::make_unique<SimpleOperationCall>(
                    context, rc, env, std::move(*device),
                    SimpleOperation{
                        .settings = readSettings("read_memory"),
                        .consent = {},
                        .details = {},
                        .action = {},
                        .start =
                            [targetId, address, length, width](DeviceSession& s) {
                                return s.readMemory(targetId, address, length, width);
                            },
                        .result = [address](const OperationResult& r) -> hio::Result<QJsonObject> {
                            QJsonObject json;
                            json.insert(QStringLiteral("address"), formatAddress(address));
                            json.insert(QStringLiteral("length"),
                                        static_cast<qint64>(r.data.size()));
                            json.insert(QStringLiteral("hex"), hexString(r.data));
                            json.insert(QStringLiteral("dump"), hexdump(address, r.data));
                            return json;
                        }});
            },
            .validate = [](const QJsonObject& args, const hio::mcp::Limits& limits)
                -> std::optional<hio::mcp::ValidationError> {
                const auto length =
                    static_cast<std::size_t>(args.value(QStringLiteral("length")).toInteger());
                if (!args.contains(QStringLiteral("to_file"))) {
                    if (length > limits.tools.readMemoryInlineBytes) {
                        return invalid(QStringLiteral("length"),
                                       QStringLiteral("must be at most %1 without to_file")
                                           .arg(limits.tools.readMemoryInlineBytes));
                    }
                    return std::nullopt;
                }
                if (length > limits.tools.readMemoryFileBytes) {
                    return invalid(
                        QStringLiteral("length"),
                        QStringLiteral("must be at most %1").arg(limits.tools.readMemoryFileBytes));
                }
                const QString name = args.value(QStringLiteral("to_file")).toString();
                const fs::path file(name.toStdU16String());
                if (file.is_absolute()) {
                    return std::nullopt;
                }
                // Review R15: on every OS a bare name has no separators and no ':' (a Windows
                // drive-relative name "C:foo" or an alternate stream "name:stream" would escape
                // the cache folder).
                const bool bare = !name.contains(QLatin1Char('/')) &&
                                  !name.contains(QLatin1Char('\\')) &&
                                  !name.contains(QLatin1Char(':')) && name != QStringLiteral(".") &&
                                  name != QStringLiteral("..");
                if (!bare) {
                    return invalid(QStringLiteral("to_file"),
                                   QStringLiteral("must be an absolute path or a bare file "
                                                  "name without '/', '\\' and ':'"));
                }
                if (isWindowsReservedName(name)) {
                    return invalid(QStringLiteral("to_file"),
                                   QStringLiteral("must not be a Windows device name (CON, PRN, "
                                                  "AUX, NUL, COM1-COM9, LPT1-LPT9) and must not "
                                                  "end with a dot or a space"));
                }
                return std::nullopt;
            }});
}

hio::Status addReadRegisters(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "read_registers",
            .title = "Read core registers",
            .description = readRegistersDescription,
            .inputSchema = deviceTargetSchema(),
            .outputSchema =
                objectOf({{"registers", typed("object")}}, {QStringLiteral("registers")}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start = operationStart(
                context,
                [](const QJsonObject&, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits& limits) -> hio::Result<SimpleOperation> {
                    const std::string id(target.id);
                    const std::size_t max = limits.maxRegisters;
                    return SimpleOperation{
                        .settings = readSettings("read_registers"),
                        .consent = {},
                        .details = {},
                        .action = {},
                        .start = [id](DeviceSession& s) { return s.readRegisters(id); },
                        .result = [max](const OperationResult& r) -> hio::Result<QJsonObject> {
                            QJsonObject registers;
                            std::size_t count = 0;
                            for (const auto& reg : r.registers) {
                                if (++count > max) {
                                    break;
                                }
                                registers.insert(qs(reg.name), formatAddress(reg.value));
                            }
                            QJsonObject json;
                            json.insert(QStringLiteral("registers"), registers);
                            return json;
                        }};
                }),
            .validate = {}});
}

hio::Status addMemoryZones(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "memory_zones",
            .title = "Memory zones",
            .description = memoryZonesDescription,
            .inputSchema = deviceTargetSchema(),
            .outputSchema =
                objectOf({{"zones", arrayOf(objectOf({{"name", typed("string")},
                                                      {"description", typed("string")},
                                                      {"virtual_address", typed("string")}}))}},
                         {QStringLiteral("zones")}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start = operationStart(
                context,
                [](const QJsonObject&, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits& limits) -> hio::Result<SimpleOperation> {
                    const std::string id(target.id);
                    const std::size_t max = limits.maxZones;
                    return SimpleOperation{
                        .settings = readSettings("memory_zones"),
                        .consent = {},
                        .details = {},
                        .action = {},
                        .start = [id](DeviceSession& s) { return s.memoryZones(id); },
                        .result = [max](const OperationResult& r) -> hio::Result<QJsonObject> {
                            QJsonArray zones;
                            for (const auto& zone : r.zones) {
                                if (std::cmp_greater_equal(zones.size(), max)) {
                                    break;
                                }
                                QJsonObject item;
                                item.insert(QStringLiteral("name"), qs(zone.name));
                                item.insert(QStringLiteral("description"), qs(zone.description));
                                item.insert(QStringLiteral("virtual_address"),
                                            formatAddress64(zone.virtualAddress));
                                zones.append(item);
                            }
                            QJsonObject json;
                            json.insert(QStringLiteral("zones"), zones);
                            return json;
                        }};
                }),
            .validate = {}});
}

hio::Status addPibAndUicr(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const bool pib : {true, false}) {
        const std::string_view name = pib ? "read_pib" : "read_uicr";
        if (auto added = addTool(
                registry,
                hio::mcp::ToolSpec{
                    .name = name,
                    .title = pib ? "Read the PIB" : "Read the UICR customer area",
                    .description = pib ? readPibDescription : readUicrDescription,
                    .inputSchema = deviceOnlySchema(),
                    .outputSchema = pib ? objectOf({{"signature", typed("string")},
                                                    {"version", typed("string")},
                                                    {"size", typed("string")},
                                                    {"crc", typed("string")},
                                                    {"vendor_name", typed("string")},
                                                    {"product_name", typed("string")},
                                                    {"hw_variant", typed("string")},
                                                    {"hw_revision", typed("string")},
                                                    {"serial_number", typed("string")},
                                                    {"claim_token", typed("string")},
                                                    {"ble_passkey", typed("string")}})
                                        : objectOf({{"format", typed("string")},
                                                    {"file", typed("string")},
                                                    {"bytes", typed("integer")},
                                                    {"hex", typed("string")}},
                                                   {QStringLiteral("hex")}),
                    .annotations = readOnly,
                    .consent = {},
                    .marker = false,
                    .start = operationStart(
                        context,
                        [pib, name](const QJsonObject&, const ResolvedDevice&,
                                    const hio::device::TargetProfile& target,
                                    const hio::mcp::ToolLimits&) -> hio::Result<SimpleOperation> {
                            const std::string id(target.id);
                            const auto address =
                                static_cast<std::uint32_t>(hio::device::pibRange(target).start);
                            return SimpleOperation{
                                .settings = readSettings(name),
                                .consent = {},
                                .details = {},
                                .action = {},
                                .start =
                                    [id, address](DeviceSession& s) {
                                        return s.readMemory(id, address, hio::device::pibSize);
                                    },
                                .result =
                                    [pib](const OperationResult& r) -> hio::Result<QJsonObject> {
                                    return pib ? pibJson(r.data) : uicrJson(r.data);
                                }};
                        },
                        "app"),
                    .validate = {}});
            !added) {
            return added;
        }
    }
    return {};
}

hio::Status addWriteMemory(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "write_memory",
            .title = "Write memory",
            .description = writeMemoryDescription,
            .inputSchema = writeMemoryInputSchema(),
            .outputSchema = addressedSchema({{"units_written", typed("integer")}}),
            .annotations = destructive,
            .consent = hio::mcp::ConsentCategory::MemoryWrite,
            .marker = true,
            .start = operationStart(
                context,
                [](const QJsonObject& args, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits& limits) -> hio::Result<SimpleOperation> {
                    auto data = dataArgument(args, limits.writeMemoryMaxBytes);
                    if (!data) {
                        return std::unexpected(std::move(data).error());
                    }
                    const std::string id(target.id);
                    const auto address =
                        parseAddress(args.value(QStringLiteral("address")).toString()).value_or(0);
                    const AccessWidth width = widthOf(args);
                    const std::size_t size = data->size();
                    // Review F4: the range (ADR-0160, ADR-0164) before the consent request.
                    if (hio::Status allowed = hio::device::checkMemoryWrite(target, address, size);
                        !allowed) {
                        return std::unexpected(std::move(allowed).error());
                    }
                    QJsonObject details;
                    details.insert(QStringLiteral("target"), qs(id));
                    details.insert(QStringLiteral("range"), rangeText(address, size));
                    details.insert(QStringLiteral("size"), static_cast<qint64>(size));
                    details.insert(QStringLiteral("width"),
                                   static_cast<int>(hio::jlink::unitBytes(width) * 8));
                    if (args.contains(QStringLiteral("path"))) {
                        details.insert(QStringLiteral("file"),
                                       args.value(QStringLiteral("path")).toString());
                    }
                    auto shared = std::make_shared<std::vector<std::byte>>(std::move(*data));
                    return SimpleOperation{
                        .settings = OperationSettings{.tool = "write_memory",
                                                      .marker = true,
                                                      .busy = false,
                                                      .cursors = false,
                                                      .progress = false},
                        .consent = hio::mcp::ConsentCategory::MemoryWrite,
                        .details = details,
                        .action = QStringLiteral("write_memory %1 (%2 bytes)")
                                      .arg(formatAddress(address), QString::number(size)),
                        .start =
                            [id, address, width, shared](DeviceSession& s) {
                                return s.writeMemory(id, address, *shared, width);
                            },
                        .result = [address, size,
                                   width](const OperationResult&) -> hio::Result<QJsonObject> {
                            QJsonObject json;
                            json.insert(QStringLiteral("address"), formatAddress(address));
                            json.insert(QStringLiteral("units_written"),
                                        static_cast<qint64>(size / hio::jlink::unitBytes(width)));
                            return json;
                        }};
                }),
            .validate = [](const QJsonObject& args, const hio::mcp::Limits& limits) {
                return checkData(args, limits.tools.writeMemoryMaxBytes);
            }});
}

hio::Status addWriteFlash(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "write_flash",
            .title = "Write flash",
            .description = writeFlashDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("target"), targetArgument())
                    .property(QStringLiteral("address"),
                              Schema::address(QStringLiteral("Flash address, \"0x...\".")),
                              Required::Yes)
                    .property(QStringLiteral("data_hex"),
                              Schema::string(QStringLiteral("Bytes as hex digits."))
                                  .minLength(2)
                                  .maxLength((2 * 1024 * 1024) + 65536))
                    .property(QStringLiteral("path"),
                              Schema::string(QStringLiteral("Absolute path to a raw binary file."))
                                  .minLength(1)
                                  .maxLength(1024))
                    .property(QStringLiteral("allow_uicr"),
                              Schema::boolean(QStringLiteral("Allow writing into the UICR."))
                                  .defaultValue(false))
                    .json(),
            .outputSchema = addressedSchema({{"bytes", typed("integer")},
                                             {"duration", typed("number")},
                                             {"log_cursor_end", typed("integer")},
                                             {"terminal_cursor_end", typed("integer")}}),
            .annotations = destructive,
            .consent = hio::mcp::ConsentCategory::Flash,
            .marker = true,
            .start = operationStart(
                context,
                [](const QJsonObject& args, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits& limits) -> hio::Result<SimpleOperation> {
                    auto data = dataArgument(args, limits.writeFlashMaxBytes);
                    if (!data) {
                        return std::unexpected(std::move(data).error());
                    }
                    const std::string id(target.id);
                    const auto address =
                        parseAddress(args.value(QStringLiteral("address")).toString()).value_or(0);
                    const bool allowUicr = args.value(QStringLiteral("allow_uicr")).toBool();
                    const std::size_t size = data->size();
                    // Review F4: the flash rules of planImage before the consent request.
                    if (hio::Status allowed =
                            hio::device::checkFlashWrite(target, address, size, allowUicr);
                        !allowed) {
                        return std::unexpected(std::move(allowed).error());
                    }
                    const hio::firmware::AddressRange range{.start = address,
                                                            .end = std::uint64_t{address} + size};
                    // Review S2, the rule of `flash`: flash always; uicr only with allow_uicr and
                    // bytes in the UICR (ADR-0101). Without allow_uicr UICR bytes were refused
                    // above (firmware.uicr_in_image), so no uicr consent is asked for.
                    const bool uicr = allowUicr && range.overlaps(target.uicr);
                    hio::mcp::ConsentCategories consent = hio::mcp::ConsentCategory::Flash;
                    if (uicr) {
                        consent |= hio::mcp::ConsentCategory::Uicr;
                    }
                    QJsonObject details;
                    details.insert(QStringLiteral("target"), qs(id));
                    details.insert(QStringLiteral("range"), rangeText(address, size));
                    details.insert(QStringLiteral("size"), static_cast<qint64>(size));
                    details.insert(QStringLiteral("uicr"), uicr);
                    if (args.contains(QStringLiteral("path"))) {
                        details.insert(QStringLiteral("file"),
                                       args.value(QStringLiteral("path")).toString());
                    }
                    auto shared = std::make_shared<std::vector<std::byte>>(std::move(*data));
                    return SimpleOperation{
                        .settings = OperationSettings{.tool = "write_flash",
                                                      .marker = true,
                                                      .busy = true,
                                                      .cursors = true,
                                                      .progress = true},
                        .consent = consent,
                        .details = details,
                        .action = QStringLiteral("write_flash %1 (%2 bytes)")
                                      .arg(formatAddress(address), QString::number(size)),
                        .start =
                            [id, address, shared, allowUicr](DeviceSession& s) {
                                return s.writeFlash(id, address, *shared, allowUicr);
                            },
                        .result = [address,
                                   size](const OperationResult& r) -> hio::Result<QJsonObject> {
                            QJsonObject json;
                            json.insert(QStringLiteral("address"), formatAddress(address));
                            json.insert(QStringLiteral("bytes"), static_cast<qint64>(size));
                            json.insert(QStringLiteral("duration"), seconds(r.duration));
                            return json;
                        }};
                }),
            .validate = [](const QJsonObject& args, const hio::mcp::Limits& limits) {
                return checkData(args, limits.tools.writeFlashMaxBytes);
            }});
}

} // namespace

hio::Status registerTargetTools(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add :
         {&addTargetStatus, &addReset, &addHaltAndGo, &addReadMemory, &addReadRegisters,
          &addMemoryZones, &addPibAndUicr, &addWriteMemory, &addWriteFlash}) {
        if (auto added = add(registry, context); !added) {
            return added;
        }
    }
    return {};
}

} // namespace hio::mcp::tools::detail

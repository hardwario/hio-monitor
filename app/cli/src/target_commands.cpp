#include "target_commands.h"

#include "cli_app.h"
#include "confirm.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/hw/jlink/programmer.h"
#include "operation_runner.h"
#include "output.h"
#include "probe_options.h"
#include "progress_ui.h"
#include "terminal.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDevice>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtTypes>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::cli {

namespace {

using device::AccessWidth;

constexpr std::string_view completed = "Successfully completed";
constexpr std::size_t bytesPerDumpLine = 16;
constexpr std::uint64_t addressSpace = 0x100000000ULL;

Error usage(std::string message, std::string detail) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = std::move(message);
    return error;
}

AccessWidth widthOf(int bits) {
    switch (bits) {
    case 16:
        return AccessWidth::Bits16;
    case 32:
        return AccessWidth::Bits32;
    default:
        return AccessWidth::Bits8;
    }
}

int bitsOf(AccessWidth width) { return static_cast<int>(jlink::unitBytes(width) * 8); }

// Length, alignment and the 32-bit address space (before the probe is touched).
Status checkRange(std::uint32_t address, std::size_t length, std::size_t maxLength,
                  AccessWidth width) {
    const std::size_t unit = jlink::unitBytes(width);
    const std::string detail =
        std::format("address {}, length {}, width {}", hex32(address), length, bitsOf(width));
    if (length == 0 || length > maxLength) {
        return std::unexpected(
            usage(std::format("The length must be between 1 and {} bytes.", maxLength), detail));
    }
    if (length % unit != 0 || address % unit != 0) {
        return std::unexpected(usage(
            std::format("With a {}-bit width, the address and the length must be multiples of {}.",
                        bitsOf(width), unit),
            detail));
    }
    if (address + std::uint64_t{length} > addressSpace) {
        return std::unexpected(usage("The range ends beyond the 32-bit address space.", detail));
    }
    return {};
}

// The bytes of `mem write` / `flash-write`: HEX or the content of -i FILE (exactly one).
Result<std::vector<std::byte>> inputBytes(const std::string& hex, const std::string& file,
                                          std::size_t maxBytes) {
    if (hex.empty() == file.empty()) {
        return std::unexpected(
            usage("Give the data either as HEX or with -i FILE.", hex.substr(0, 64)));
    }
    if (!hex.empty()) {
        return parseHexBytes(hex);
    }
    QFile in(toQString(file));
    if (!in.open(QIODevice::ReadOnly)) {
        return std::unexpected(
            makeError(ErrorCode::IoReadFailed, file + ": " + in.errorString().toStdString()));
    }
    // One byte more than allowed tells "too large" without reading a huge file.
    const QByteArray content = in.read(static_cast<qint64>(maxBytes) + 1);
    if (content.isEmpty() && in.error() != QFileDevice::NoError) {
        return std::unexpected(
            makeError(ErrorCode::IoReadFailed, file + ": " + in.errorString().toStdString()));
    }
    if (content.isEmpty() || std::cmp_greater(content.size(), maxBytes)) {
        return std::unexpected(
            usage(std::format("The file must contain between 1 and {} bytes.", maxBytes), file));
    }
    const auto bytes =
        std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size())));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

std::string hexOf(std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        const auto value = std::to_integer<unsigned>(b);
        text += digits[value >> 4U];
        text += digits[value & 0x0FU];
    }
    return text;
}

// "1 byte", "4 bytes".
std::string byteCount(std::size_t count) {
    return std::format("{} {}", count, count == 1 ? "byte" : "bytes");
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// ---- status / halt / go / regs / zones ------------------------------------------------------

// One light operation with a quiet ProgressUi (one result envelope, like `reset`).
Result<device::OperationResult> runQuiet(const Context& context, const GlobalOptions& globals,
                                         const ProbeOptions& group, const ProbeOptions& own,
                                         std::string_view targetId, const StartFn& start) {
    ProgressUi ui(context, std::cerr, false, true);
    auto outcome = runOperation(context, globals, group, own, targetId, ui, start);
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    return std::move(outcome->result);
}

Result<CommandResult> status(const Context& context, const GlobalOptions& globals,
                             const ProbeOptions& group, const ProbeOptions& own,
                             const std::string& targetId) {
    auto result = runQuiet(context, globals, group, own, targetId,
                           [&](device::DeviceSession& s) { return s.targetStatus(targetId); });
    if (!result) {
        return std::unexpected(std::move(result).error());
    }
    const device::TargetStatus target = result->targetStatus.value_or(device::TargetStatus{});
    QJsonObject data;
    data.insert(QStringLiteral("halted"), target.halted);
    // null / "unknown" without JLINKARM_GetId (review S6).
    data.insert(QStringLiteral("coreId"), target.coreId
                                              ? QJsonValue(toQString(hex32(*target.coreId)))
                                              : QJsonValue(QJsonValue::Null));
    data.insert(QStringLiteral("device"), toQString(target.deviceName));
    std::string text = std::format("CPU: {}\n", target.halted ? "halted" : "running");
    text += std::format("Core ID: {}\n",
                        target.coreId ? hex32(*target.coreId) : std::string("unknown"));
    text += std::format("J-Link device: {}", target.deviceName);
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

Result<CommandResult> haltOrGo(const Context& context, const GlobalOptions& globals,
                               const ProbeOptions& group, const ProbeOptions& own,
                               const std::string& targetId, bool halt) {
    auto result = runQuiet(context, globals, group, own, targetId, [&](device::DeviceSession& s) {
        return halt ? s.halt(targetId) : s.go(targetId);
    });
    if (!result) {
        return std::unexpected(std::move(result).error());
    }
    const bool halted = result->targetStatus ? result->targetStatus->halted : halt;
    QJsonObject data;
    data.insert(QStringLiteral("halted"), halted);
    return CommandResult{
        .data = data, .humanText = halted ? "CPU halted" : "CPU running", .streamed = false};
}

Result<CommandResult> regs(const Context& context, const GlobalOptions& globals,
                           const ProbeOptions& group, const ProbeOptions& own,
                           const std::string& targetId) {
    auto result = runQuiet(context, globals, group, own, targetId,
                           [&](device::DeviceSession& s) { return s.readRegisters(targetId); });
    if (!result) {
        return std::unexpected(std::move(result).error());
    }
    return CommandResult{.data = registersJson(result->registers),
                         .humanText = registersText(result->registers),
                         .streamed = false};
}

Result<CommandResult> zones(const Context& context, const GlobalOptions& globals,
                            const ProbeOptions& group, const ProbeOptions& own,
                            const std::string& targetId) {
    auto result = runQuiet(context, globals, group, own, targetId,
                           [&](device::DeviceSession& s) { return s.memoryZones(targetId); });
    if (!result) {
        return std::unexpected(std::move(result).error());
    }
    return CommandResult{
        .data = zonesJson(result->zones), .humanText = zonesText(result->zones), .streamed = false};
}

// ---- mem read --------------------------------------------------------------------------------

struct MemReadOptions {
    ProbeOptions probe;
    std::string address;
    std::string length = "64";
    int width = 8;
    std::string file;
};

// -o FILE: created exclusively (never overwritten); removed again when the read fails.
class OutputFile {
public:
    explicit OutputFile(const std::string& path) : m_file(toQString(path)), m_path(path) {}
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    OutputFile(OutputFile&&) = delete;
    OutputFile& operator=(OutputFile&&) = delete;
    ~OutputFile() {
        if (m_created && !m_kept) {
            m_file.close();
            m_file.remove();
        }
    }

    [[nodiscard]] Status create() {
        if (m_file.exists()) {
            return std::unexpected(makeError(ErrorCode::IoFileExists, m_path));
        }
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (m_file.exists()) {
                return std::unexpected(makeError(ErrorCode::IoFileExists, m_path));
            }
            return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                             m_path + ": " + m_file.errorString().toStdString()));
        }
        m_created = true;
        return {};
    }

    [[nodiscard]] Status write(std::span<const std::byte> bytes) {
        const auto size = static_cast<qint64>(bytes.size());
        if (m_file.write(QByteArrayView(bytes.data(), size).data(), size) != size) {
            return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                             m_path + ": " + m_file.errorString().toStdString()));
        }
        return {};
    }

    [[nodiscard]] Status keep() {
        if (!m_file.flush()) {
            return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                             m_path + ": " + m_file.errorString().toStdString()));
        }
        m_file.close();
        m_kept = true;
        return {};
    }

private:
    QFile m_file;
    std::string m_path;
    bool m_created = false;
    bool m_kept = false;
};

Result<CommandResult> memRead(const Context& context, const GlobalOptions& globals,
                              const ProbeOptions& group, const ProbeOptions& own,
                              const std::string& targetId, const MemReadOptions& options) {
    auto address = parseAddress(options.address);
    if (!address) {
        return std::unexpected(std::move(address).error());
    }
    auto length = parseAddress(options.length);
    if (!length) {
        Error error = std::move(length).error();
        error.message = "The length must be a decimal number or 0x followed by hex digits.";
        return std::unexpected(std::move(error));
    }
    const AccessWidth width = widthOf(options.width);
    if (Status range = checkRange(*address, *length, maxMemReadBytes, width); !range) {
        return std::unexpected(std::move(range).error());
    }
    std::optional<OutputFile> file;
    if (!options.file.empty()) {
        file.emplace(options.file);
        if (Status created = file->create(); !created) {
            return std::unexpected(std::move(created).error());
        }
    }
    ProgressUi ui(context, std::cerr, false, true);
    auto runner = OperationRunner::open(context, globals, group, own, targetId, ui);
    if (!runner) {
        return std::unexpected(std::move(runner).error());
    }
    std::vector<std::byte> bytes; // stdout only (<= maxMemReadBytes)
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    device::OperationResult last;
    for (std::size_t offset = 0; offset < *length; offset += memReadChunkBytes) {
        const std::size_t chunk = std::min(memReadChunkBytes, std::size_t{*length} - offset);
        const auto chunkAddress = static_cast<std::uint32_t>(*address + offset);
        auto result = (*runner)->run([&](device::DeviceSession& s) {
            return s.readMemory(targetId, chunkAddress, chunk, width);
        });
        if (!result) {
            return std::unexpected(std::move(result).error());
        }
        if (result->data.size() != chunk) {
            return std::unexpected(makeError(
                ErrorCode::InternalUnexpected,
                std::format("short memory read: {} of {} bytes", result->data.size(), chunk)));
        }
        if (file) {
            if (Status written = file->write(result->data); !written) {
                return std::unexpected(std::move(written).error());
            }
            sha256.addData(
                QByteArrayView(result->data.data(), static_cast<qsizetype>(result->data.size())));
        } else {
            bytes.insert(bytes.end(), result->data.begin(), result->data.end());
        }
        last = std::move(*result);
    }
    (*runner)->close();
    (*runner)->showTarget(last);
    QJsonObject data;
    data.insert(QStringLiteral("address"), toQString(hex32(*address)));
    data.insert(QStringLiteral("length"), static_cast<double>(*length));
    data.insert(QStringLiteral("width"), options.width);
    if (file) {
        if (Status kept = file->keep(); !kept) {
            return std::unexpected(std::move(kept).error());
        }
        data.insert(QStringLiteral("file"), toQString(options.file));
        data.insert(QStringLiteral("sha256"), QString::fromLatin1(sha256.result().toHex()));
        return CommandResult{.data = data,
                             .humanText = std::format("Read {} from {} into {}", byteCount(*length),
                                                      hex32(*address), options.file),
                             .streamed = false};
    }
    data.insert(QStringLiteral("hex"), toQString(hexOf(bytes)));
    return CommandResult{.data = data,
                         .humanText = context.mode == OutputMode::Human
                                          ? hexDump(*address, bytes, width)
                                          : std::string(),
                         .streamed = false};
}

// ---- mem write / flash-write ----------------------------------------------------------------

struct MemWriteOptions {
    ProbeOptions probe;
    std::string address;
    std::string hex;
    std::string input;
    int width = 8;
    bool yes = false;
};

Result<CommandResult> memWrite(const Context& context, const GlobalOptions& globals,
                               const ProbeOptions& group, const ProbeOptions& own,
                               const std::string& targetId, const MemWriteOptions& options) {
    auto address = parseAddress(options.address);
    if (!address) {
        return std::unexpected(std::move(address).error());
    }
    auto bytes = inputBytes(options.hex, options.input, device::maxWriteMemoryBytes);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    const AccessWidth width = widthOf(options.width);
    const std::size_t length = bytes->size();
    if (Status range = checkRange(*address, length, device::maxWriteMemoryBytes, width); !range) {
        return std::unexpected(std::move(range).error());
    }
    // Review S8: the target range (ADR-0160) before the confirmation question.
    if (Status allowed = device::checkMemoryWrite(chesterTarget(targetId), *address, length);
        !allowed) {
        return std::unexpected(std::move(allowed).error());
    }
    const std::string question =
        std::format("This writes {} to the memory of the {} MCU at {}.\nContinue? [y/N] ",
                    byteCount(length), chesterTarget(targetId).displayName, hex32(*address));
    if (Status confirmed = confirmDestructive(context, options.yes, question); !confirmed) {
        return std::unexpected(std::move(confirmed).error());
    }
    auto result = runQuiet(context, globals, group, own, targetId, [&](device::DeviceSession& s) {
        return s.writeMemory(targetId, *address, std::move(*bytes), width);
    });
    if (!result) {
        return std::unexpected(std::move(result).error());
    }
    QJsonObject data;
    data.insert(QStringLiteral("address"), toQString(hex32(*address)));
    data.insert(QStringLiteral("length"), static_cast<double>(length));
    data.insert(QStringLiteral("width"), options.width);
    return CommandResult{.data = data,
                         .humanText =
                             std::format("Wrote {} to {}", byteCount(length), hex32(*address)),
                         .streamed = false};
}

struct FlashWriteOptions {
    ProbeOptions probe;
    std::string address;
    std::string hex;
    std::string input;
    bool allowUicr = false;
    bool yes = false;
};

Result<CommandResult> flashWrite(const Context& context, const GlobalOptions& globals,
                                 const ProbeOptions& group, const ProbeOptions& own,
                                 const std::string& targetId, const FlashWriteOptions& options) {
    auto address = parseAddress(options.address);
    if (!address) {
        return std::unexpected(std::move(address).error());
    }
    auto bytes = inputBytes(options.hex, options.input, device::maxWriteFlashBytes);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    const std::size_t length = bytes->size();
    if (Status range = checkRange(*address, length, device::maxWriteFlashBytes, AccessWidth::Bits8);
        !range) {
        return std::unexpected(std::move(range).error());
    }
    // Review S8: the flash-write range rules of the backend (device::checkFlashWrite, the rules
    // of planImage) before the confirmation question; the backend checks them again.
    if (Status allowed =
            device::checkFlashWrite(chesterTarget(targetId), *address, length, options.allowUicr);
        !allowed) {
        return std::unexpected(std::move(allowed).error());
    }
    const std::string question =
        std::format("This programs {} into the flash of the {} MCU at {}.\nContinue? [y/N] ",
                    byteCount(length), chesterTarget(targetId).displayName, hex32(*address));
    if (Status confirmed = confirmDestructive(context, options.yes, question); !confirmed) {
        return std::unexpected(std::move(confirmed).error());
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    auto outcome =
        runOperation(context, globals, group, own, targetId, ui, [&](device::DeviceSession& s) {
            return s.writeFlash(targetId, *address, std::move(*bytes), options.allowUicr);
        });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data;
    data.insert(QStringLiteral("type"), QStringLiteral("result"));
    data.insert(QStringLiteral("target"), toQString(targetId));
    data.insert(QStringLiteral("address"), toQString(hex32(*address)));
    data.insert(QStringLiteral("length"), static_cast<double>(length));
    data.insert(QStringLiteral("durationMs"),
                static_cast<double>(outcome->result.duration.count()));
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

// ---- registration helpers --------------------------------------------------------------------

void addWidthOption(CLI::App& command, int& width) {
    command.add_option("-w,--width", width, "Access width in bits [8]")
        ->check(CLI::IsMember({8, 16, 32}))
        ->option_text("{8,16,32}");
}

void addAddressArgument(CLI::App& command, std::string& address) {
    command.add_option("address", address, "Start address (decimal or 0x hex)")
        ->required()
        ->option_text("ADDR");
}

void addDataOptions(CLI::App& command, std::string& hex, std::string& input) {
    command.add_option("hex", hex, "The bytes as hex digits in address order, e.g. 0102aabb")
        ->option_text("HEX");
    command.add_option("-i,--input", input, "Read the bytes from FILE instead")
        ->option_text("FILE");
}

// The subcommand's probe options win over those of an intermediate group (`mem`), as for
// `pib read` / `uicr read`.
ProbeOptions merged(const ProbeOptions& middle, const ProbeOptions& own) {
    return ProbeOptions{.serialNumber = serialNumberOf(middle, own),
                        .speed = own.speed ? own.speed : middle.speed};
}

using SimpleAction =
    std::function<Result<CommandResult>(const Context&, const GlobalOptions&, const ProbeOptions&,
                                        const ProbeOptions&, const std::string&)>;

// A command without options of its own beyond the probe options.
void addSimpleCommand(CLI::App& targetGroup, const std::shared_ptr<ProbeOptions>& group,
                      CommandSlot& slot, const GlobalOptions& globals, const std::string& targetId,
                      const std::string& name, const std::string& description,
                      const SimpleAction& action) {
    auto own = std::make_shared<ProbeOptions>();
    CLI::App* command = targetGroup.add_subcommand(name, description);
    addProbeOptions(*command, *own);
    command->callback([&slot, &globals, group, own, targetId, action] {
        slot.command = [&globals, group, own, targetId, action](const Context& context) {
            return action(context, globals, *group, *own, targetId);
        };
    });
}

Result<CommandResult> halt(const Context& context, const GlobalOptions& globals,
                           const ProbeOptions& group, const ProbeOptions& own,
                           const std::string& targetId) {
    return haltOrGo(context, globals, group, own, targetId, true);
}

Result<CommandResult> go(const Context& context, const GlobalOptions& globals,
                         const ProbeOptions& group, const ProbeOptions& own,
                         const std::string& targetId) {
    return haltOrGo(context, globals, group, own, targetId, false);
}

} // namespace

std::string hex32(std::uint64_t value) { return std::format("0x{:08X}", value); }

Result<std::uint32_t> parseAddress(std::string_view text) {
    const auto invalid = [text] {
        return std::unexpected(
            usage("The address must be a decimal number or 0x followed by 1 to 8 hex digits.",
                  std::string(text)));
    };
    std::uint64_t value = 0;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        const std::string_view digits = text.substr(2);
        if (digits.empty() || digits.size() > 8) {
            return invalid();
        }
        for (const char c : digits) {
            const int digit = hexDigit(c);
            if (digit < 0) {
                return invalid();
            }
            value = (value << 4U) | static_cast<std::uint64_t>(digit);
        }
        return static_cast<std::uint32_t>(value);
    }
    if (text.empty() || text.size() > 10) {
        return invalid();
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return invalid();
        }
        value = (value * 10) + static_cast<std::uint64_t>(c - '0');
    }
    if (value >= addressSpace) {
        return invalid();
    }
    return static_cast<std::uint32_t>(value);
}

Result<std::vector<std::byte>> parseHexBytes(std::string_view text) {
    if (text.empty() || text.size() % 2 != 0) {
        return std::unexpected(usage("HEX must be an even number of hex digits (two per byte).",
                                     std::string(text.substr(0, 64))));
    }
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = hexDigit(text[i]);
        const int low = hexDigit(text[i + 1]);
        if (high < 0 || low < 0) {
            return std::unexpected(usage("HEX must contain hex digits only (0-9, a-f).",
                                         std::string(text.substr(0, 64))));
        }
        bytes.push_back(static_cast<std::byte>((high << 4) | low));
    }
    return bytes;
}

std::string hexDump(std::uint32_t address, std::span<const std::byte> data, AccessWidth width) {
    const std::size_t unit = jlink::unitBytes(width);
    const std::size_t unitsPerLine = bytesPerDumpLine / unit;
    // Hex column of a full line: units with one space each, one more in the middle.
    const std::size_t hexColumn = (unitsPerLine * ((unit * 2) + 1)) + 1;
    std::string text;
    for (std::size_t offset = 0; offset < data.size(); offset += bytesPerDumpLine) {
        const auto line = data.subspan(offset, std::min(bytesPerDumpLine, data.size() - offset));
        std::string hex;
        for (std::size_t u = 0; u * unit < line.size(); ++u) {
            if (u == unitsPerLine / 2) {
                hex += ' ';
            }
            const auto units = line.subspan(u * unit, std::min(unit, line.size() - (u * unit)));
            std::uint32_t value = 0;
            for (std::size_t b = units.size(); b > 0; --b) { // little-endian
                value = (value << 8U) | std::to_integer<std::uint32_t>(units[b - 1]);
            }
            hex += std::format(" {:0{}x}", value, units.size() * 2);
        }
        std::string ascii;
        for (const std::byte b : line) {
            const auto c = std::to_integer<unsigned char>(b);
            ascii += c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.';
        }
        if (!text.empty()) {
            text += '\n';
        }
        text += std::format("{} {:<{}}  |{}|", hex32(address + offset), hex, hexColumn, ascii);
    }
    return text;
}

std::string registersText(std::span<const device::RegisterValue> registers) {
    std::size_t nameWidth = 0;
    for (const auto& reg : registers) {
        nameWidth = std::max(nameWidth, reg.name.size());
    }
    std::string text;
    for (const auto& reg : registers) {
        if (!text.empty()) {
            text += '\n';
        }
        text += std::format("{:<{}}  {}", reg.name, nameWidth, hex32(reg.value));
    }
    return text;
}

QJsonObject registersJson(std::span<const device::RegisterValue> registers) {
    QJsonObject values;
    for (const auto& reg : registers) {
        values.insert(toQString(reg.name), toQString(hex32(reg.value)));
    }
    QJsonObject data;
    data.insert(QStringLiteral("registers"), values);
    return data;
}

std::string zonesText(std::span<const device::MemoryZone> zones) {
    if (zones.empty()) {
        return "No memory zones.";
    }
    std::size_t nameWidth = 0;
    for (const auto& zone : zones) {
        nameWidth = std::max(nameWidth, zone.name.size());
    }
    std::string text;
    for (const auto& zone : zones) {
        if (!text.empty()) {
            text += '\n';
        }
        text += std::format("{:<{}}  {}  {}", zone.name, nameWidth, hex32(zone.virtualAddress),
                            zone.description);
    }
    return text;
}

QJsonObject zonesJson(std::span<const device::MemoryZone> zones) {
    QJsonArray list;
    for (const auto& zone : zones) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), toQString(zone.name));
        item.insert(QStringLiteral("description"), toQString(zone.description));
        item.insert(QStringLiteral("virtualAddress"), toQString(hex32(zone.virtualAddress)));
        list.append(item);
    }
    QJsonObject data;
    data.insert(QStringLiteral("zones"), list);
    return data;
}

void registerTargetCommands(CLI::App& targetGroup, const std::shared_ptr<ProbeOptions>& group,
                            CommandSlot& slot, const GlobalOptions& globals,
                            std::string_view targetIdView) {
    const std::string targetId(targetIdView);
    addSimpleCommand(targetGroup, group, slot, globals, targetId, "status",
                     "Show whether the CPU is halted, its core ID and the J-Link device", status);
    addSimpleCommand(targetGroup, group, slot, globals, targetId, "halt",
                     "Halt the CPU (without a reset)", halt);
    addSimpleCommand(targetGroup, group, slot, globals, targetId, "go",
                     "Let the halted CPU run (without a reset)", go);
    addSimpleCommand(targetGroup, group, slot, globals, targetId, "regs",
                     "Show the CPU registers (the CPU must be halted)", regs);
    addSimpleCommand(targetGroup, group, slot, globals, targetId, "zones",
                     "List the memory zones the J-Link library reports for the target", zones);

    CLI::App* mem = targetGroup.add_subcommand("mem", "Read or write target memory");
    mem->require_subcommand(1);
    auto memOptions = std::make_shared<ProbeOptions>();
    addProbeOptions(*mem, *memOptions);

    auto readOptions = std::make_shared<MemReadOptions>();
    CLI::App* read = mem->add_subcommand(
        "read", "Print target memory as a hexdump, or save the raw bytes to a new FILE");
    addAddressArgument(*read, readOptions->address);
    read->add_option("-l,--length", readOptions->length,
                     "Number of bytes (decimal or 0x hex, at most 16 MiB) [64]")
        ->option_text("LEN");
    addWidthOption(*read, readOptions->width);
    read->add_option("-o,--output", readOptions->file,
                     "Save the raw bytes to FILE (it must not exist)")
        ->option_text("FILE");
    addProbeOptions(*read, readOptions->probe);
    read->callback([&slot, &globals, group, memOptions, readOptions, targetId] {
        slot.command = [&globals, group, memOptions, readOptions,
                        targetId](const Context& context) {
            return memRead(context, globals, *group, merged(*memOptions, readOptions->probe),
                           targetId, *readOptions);
        };
    });

    auto writeOptions = std::make_shared<MemWriteOptions>();
    CLI::App* write =
        mem->add_subcommand("write", "Write bytes to RAM or peripheral registers (at most 64 KiB)");
    // ADR-0164: RAM stays writable, so a memory write can still run code.
    write->footer("Writing memory can make the device run arbitrary code, which can also erase its "
                  "flash and the PIB.");
    addAddressArgument(*write, writeOptions->address);
    addDataOptions(*write, writeOptions->hex, writeOptions->input);
    addWidthOption(*write, writeOptions->width);
    write->add_flag("--yes", writeOptions->yes, "Do not ask for confirmation");
    addProbeOptions(*write, writeOptions->probe);
    write->callback([&slot, &globals, group, memOptions, writeOptions, targetId] {
        slot.command = [&globals, group, memOptions, writeOptions,
                        targetId](const Context& context) {
            return memWrite(context, globals, *group, merged(*memOptions, writeOptions->probe),
                            targetId, *writeOptions);
        };
    });

    auto flashOptions = std::make_shared<FlashWriteOptions>();
    CLI::App* flashWriteCommand = targetGroup.add_subcommand(
        "flash-write", "Program raw bytes into flash (at most 1 MiB; the other bytes of each "
                       "flash page are kept)");
    addAddressArgument(*flashWriteCommand, flashOptions->address);
    addDataOptions(*flashWriteCommand, flashOptions->hex, flashOptions->input);
    flashWriteCommand->add_flag(
        "--allow-uicr", flashOptions->allowUicr,
        "Allow writing into the UICR (the Product Information Block stays protected)");
    flashWriteCommand->add_flag("--yes", flashOptions->yes, "Do not ask for confirmation");
    addProbeOptions(*flashWriteCommand, flashOptions->probe);
    flashWriteCommand->callback([&slot, &globals, group, flashOptions, targetId] {
        slot.command = [&globals, group, flashOptions, targetId](const Context& context) {
            return flashWrite(context, globals, *group, flashOptions->probe, targetId,
                              *flashOptions);
        };
    });
}

} // namespace hio::cli

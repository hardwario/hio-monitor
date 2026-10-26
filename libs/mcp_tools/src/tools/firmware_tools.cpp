// Firmware tools (plan/features/0013 C2, ADR-0099, ADR-0100, ADR-0101): flash, flash_lte, erase,
// recover. After a successful run they write a marker and return new cursors; the ConsoleLog is
// not cleared (the AI reads only new output with after_cursor).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"
#include "hio/firmware/lte_input.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/offload.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"
#include "hio/mcp/tools/tools_context.h"
#include "tool_support.h"
#include "tools/tools.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace hio::mcp::tools::detail {

namespace {

namespace fs = std::filesystem;
using hio::device::DeviceSession;
using hio::device::OperationResult;
using hio::mcp::Schema;

constexpr const char* flashDescription =
    "Flash an application firmware image (Intel HEX) into the device: either `path`, an absolute "
    "path to a .hex file on this machine (at most 64 MiB), or `catalog_id`, the 32-digit id of a "
    "HARDWARIO catalog firmware (downloaded and cached) - exactly one of them. The device is "
    "programmed and verified, reset, and the console re-attaches by itself; the UICR and the "
    "Product Information Block are kept unless allow_uicr=true (UICR records of the image are "
    "then programmed; the PIB is always kept). Typically 10-60 s (progress is reported). Needs "
    "the user's permission (flash; uicr with allow_uicr). The console history is NOT cleared: "
    "read only the new firmware's output with read_log / read_terminal "
    "after_cursor=log_cursor_end of the result, and use wait_for_connection to wait for the "
    "console. Common images: build/merged.hex, build/zephyr/merged.hex. J-Link only.";

constexpr const char* flashLteDescription =
    "Update the LTE modem MCU (CHESTER target \"lte\"): `path` is an absolute path to a Nordic "
    "modem firmware zip (mfw_nrf91x...zip), an LTE bundle zip (modem.zip + application.hex) or "
    "an application .hex for the LTE MCU. Modem firmware updates take several minutes (typically "
    "3-10 min) and need nRF Util; progress is reported. Needs the user's permission (flash). "
    "Afterwards read new output with after_cursor=log_cursor_end of the result. J-Link only.";

constexpr const char* eraseDescription =
    "Erase the flash of a target MCU. scope \"code\" keeps the UICR and the Product Information "
    "Block; \"all\" erases flash and UICR, and the Product Information Block is lost. Default: "
    "the target's usual scope (CHESTER app: code, lte: all). The firmware is gone afterwards. "
    "Takes a few seconds. Needs the user's permission: erase for scope \"code\"; erase and uicr "
    "for scope \"all\" (it also removes the Product Information Block). J-Link only.";

constexpr const char* recoverDescription =
    "Recover a locked (read-back protected) MCU: erase all flash, UICR and the Product "
    "Information Block through the CTRL-AP and remove the protection (nRF91: then program the "
    "disable-AP-protect image). Takes up to 60 s. Needs the user's permission (erase and uicr: "
    "the Product Information Block is lost). J-Link only.";

QJsonObject operationOutputSchema() {
    return objectOf({{"status", typed("string")},
                     {"file", typed("string")},
                     {"catalog_id", typed("string")},
                     {"target", typed("string")},
                     {"operation", typed("string")},
                     {"duration", typed("number")},
                     {"chip", nullable("string")},
                     {"verified", typed("boolean")},
                     {"log_cursor_end", typed("integer")},
                     {"terminal_cursor_end", typed("integer")}},
                    {QStringLiteral("status"), QStringLiteral("target")});
}

QJsonObject operationJson(std::string_view target, const OperationResult& result) {
    QJsonObject json;
    json.insert(QStringLiteral("status"), QStringLiteral("ok"));
    json.insert(QStringLiteral("target"), qs(target));
    json.insert(QStringLiteral("operation"), qs(hio::device::operationKindId(result.kind)));
    json.insert(QStringLiteral("duration"), seconds(result.duration));
    json.insert(QStringLiteral("chip"), result.chip && !result.chip->name.empty()
                                            ? QJsonValue(qs(result.chip->name))
                                            : QJsonValue(QJsonValue::Null));
    return json;
}

constexpr OperationSettings programSettings(std::string_view tool) {
    return OperationSettings{
        .tool = tool, .marker = true, .busy = true, .cursors = true, .progress = true};
}

// flash: loads the image (file, or catalog download), asks consent (flash + uicr when the image
// has UICR records and allow_uicr), then runs DeviceSession::flash. The Intel HEX parse (up to
// 64 MiB) runs on an offloaded worker thread (ADR-0162); it gets only the path.
class FlashCall final : public OperationCall {
public:
    FlashCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
              const hio::mcp::ToolEnv& env, ResolvedDevice device,
              const hio::device::TargetProfile& target, const QJsonObject& args)
        : OperationCall(tools, context, env, std::move(device), programSettings("flash")),
          m_target(&target), m_allowUicr(args.value(QStringLiteral("allow_uicr")).toBool()) {
        if (args.contains(QStringLiteral("path"))) {
            m_source = args.value(QStringLiteral("path")).toString();
            m_fromFile = true;
            auto file = inputFile(m_source, limits().imageFileMaxBytes);
            if (!file) {
                completeError(std::move(file).error());
                return;
            }
            using Parsed = hio::Result<hio::firmware::FirmwareImage>;
            auto job = env.offloader().submit<Parsed>(
                *this,
                [path = *file](const std::stop_token& stop) -> Parsed {
                    // Review F2: a cancelled call (or a stopping server) ends the parse early.
                    return hio::firmware::readIntelHexFile(path, stop);
                },
                [this](Parsed image) {
                    if (!image) {
                        completeError(std::move(image).error());
                        return;
                    }
                    proceed(
                        std::make_shared<const hio::firmware::FirmwareImage>(std::move(*image)));
                });
            if (!job) {
                completeError(std::move(job).error()); // mcp.too_many_requests
                return;
            }
            m_parse = std::move(*job);
            return;
        }
        m_source = args.value(QStringLiteral("catalog_id")).toString();
        auto id = hio::firmware::FirmwareId::parse(m_source.toStdString());
        if (!id) {
            completeError(std::move(id).error());
            return;
        }
        m_source = qs(id->value());
        if (tools.catalog() == nullptr) {
            completeError(invalidArgument(
                QStringLiteral("The firmware catalog is not available in this server."),
                id->value()));
            return;
        }
        m_download = tools.catalog()->download(*id, hio::firmware::DownloadOptions{});
        connect(m_download.get(), &hio::firmware::CatalogOperation::finished, this,
                [this] { downloaded(); });
        if (m_download->isFinished()) {
            downloaded();
        }
    }

protected:
    void cancelPending() override {
        m_parse.cancel();
        if (m_download && !m_download->isFinished()) {
            m_download->cancel();
        }
    }

private:
    void downloaded() {
        if (isFinished() || !m_download || !m_download->result()) {
            return;
        }
        auto result = *m_download->result();
        if (!result) {
            completeError(std::move(result).error());
            return;
        }
        proceed(result->image);
    }

    void proceed(std::shared_ptr<const hio::firmware::FirmwareImage> image) {
        if (isFinished()) {
            return;
        }
        m_image = std::move(image);
        hio::mcp::ConsentCategories consent = hio::mcp::ConsentCategory::Flash;
        const bool uicr = m_image->intersects(m_target->uicr);
        if (uicr && m_allowUicr) {
            consent |= hio::mcp::ConsentCategory::Uicr; // ADR-0101
        }
        QJsonObject details;
        details.insert(m_fromFile ? QStringLiteral("file") : QStringLiteral("catalog_id"),
                       m_source);
        details.insert(QStringLiteral("target"), qs(m_target->id));
        details.insert(QStringLiteral("size"), static_cast<qint64>(m_image->totalBytes()));
        details.insert(QStringLiteral("sha256"), qs(m_image->sha256()));
        details.insert(QStringLiteral("uicr"), uicr && m_allowUicr);
        withConsent(consent, details, [this] { program(); });
    }

    void program() {
        const QString name = m_fromFile
                                 ? QString::fromStdU16String(
                                       fs::path(m_source.toStdU16String()).filename().u16string())
                                 : m_source;
        if (!begin(QStringLiteral("flash %1 (%2)").arg(name, qs(m_target->id)))) {
            return;
        }
        const std::string target(m_target->id);
        const hio::device::FlashOptions options{.halt = false, .allowUicr = m_allowUicr};
        run([this, target,
             options](DeviceSession& session) { return session.flash(target, m_image, options); },
            [this](const OperationResult& result) {
                QJsonObject json = operationJson(m_target->id, result);
                json.insert(m_fromFile ? QStringLiteral("file") : QStringLiteral("catalog_id"),
                            m_source);
                json.insert(QStringLiteral("verified"), true);
                succeed(std::move(json), result.duration);
            });
    }

    const hio::device::TargetProfile* m_target; // non-owning, static profile data
    bool m_allowUicr;
    bool m_fromFile = false;
    QString m_source;
    hio::mcp::Offloader::Handle m_parse; // the offloaded HEX parse (cancelled with the call)
    std::unique_ptr<hio::firmware::CatalogOperation> m_download;
    std::shared_ptr<const hio::firmware::FirmwareImage> m_image;
};

// flash_lte: classifies the input on an offloaded worker thread (ADR-0162: a zip is opened and a
// bundle extracted, a .hex parsed), asks consent (flash), then runs the matching operation.
class FlashLteCall final : public OperationCall {
public:
    FlashLteCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                 const hio::mcp::ToolEnv& env, ResolvedDevice device,
                 const hio::device::TargetProfile& target, QString path)
        : OperationCall(tools, context, env, std::move(device), programSettings("flash_lte")),
          m_target(target.id), m_path(std::move(path)) {
        auto file = inputFile(m_path, limits().imageFileMaxBytes);
        if (!file) {
            completeError(std::move(file).error());
            return;
        }
        m_name = QString::fromStdU16String(file->filename().u16string());
        using Classified = hio::Result<hio::firmware::LteInput>;
        auto job = env.offloader().submit<Classified>(
            *this,
            [path = *file](const std::stop_token& stop) -> Classified {
                // Review F2: the HEX parse and the zip extraction observe the stop token.
                return hio::firmware::classifyLteInput(path, stop);
            },
            [this](Classified input) {
                if (!input) {
                    completeError(std::move(input).error());
                    return;
                }
                classified(std::move(*input));
            });
        if (!job) {
            completeError(std::move(job).error()); // mcp.too_many_requests
            return;
        }
        m_classify = std::move(*job);
    }

protected:
    void cancelPending() override { m_classify.cancel(); }

private:
    void classified(hio::firmware::LteInput input) {
        if (isFinished()) {
            return;
        }
        const std::string id = m_target;
        const hio::device::FlashOptions options{.halt = false, .allowUicr = false};
        QString kind;
        if (auto* bundle = std::get_if<hio::firmware::LteBundle>(&input)) {
            kind = QStringLiteral("bundle");
            auto shared = std::make_shared<const hio::firmware::LteBundle>(std::move(*bundle));
            m_start = [id, shared, options](DeviceSession& s) {
                return s.flashLteBundle(id, shared, options);
            };
        } else if (const auto* modem = std::get_if<hio::firmware::ModemOnly>(&input)) {
            kind = QStringLiteral("modem");
            m_start = [id, zip = modem->path](DeviceSession& s) { return s.updateModem(id, zip); };
        } else {
            kind = QStringLiteral("hex");
            auto shared = std::make_shared<const hio::firmware::FirmwareImage>(
                std::move(std::get<hio::firmware::AppHex>(input).image));
            m_start = [id, shared, options](DeviceSession& s) {
                return s.flash(id, shared, options);
            };
        }
        QJsonObject details;
        details.insert(QStringLiteral("file"), m_path);
        details.insert(QStringLiteral("target"), qs(id));
        details.insert(QStringLiteral("kind"), kind);
        withConsent(hio::mcp::ConsentCategory::Flash, details, [this] { program(); });
    }

    void program() {
        if (!begin(QStringLiteral("flash_lte %1").arg(m_name))) {
            return;
        }
        run(m_start, [this](const OperationResult& result) {
            QJsonObject json = operationJson(m_target, result);
            json.insert(QStringLiteral("file"), m_path);
            succeed(std::move(json), result.duration);
        });
    }

    std::string m_target;
    QString m_path;
    QString m_name;
    OperationStart m_start;
    hio::mcp::Offloader::Handle m_classify; // the offloaded classification
};

hio::Status addFlash(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "flash",
            .title = "Flash firmware",
            .description = flashDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("path"),
                              Schema::string(QStringLiteral("Absolute path to an Intel HEX file."))
                                  .minLength(1)
                                  .maxLength(1024))
                    .property(QStringLiteral("catalog_id"),
                              Schema::string(QStringLiteral("32-digit HARDWARIO firmware id."))
                                  .pattern(QStringLiteral("^[0-9a-fA-F]{32}$")))
                    .property(QStringLiteral("target"), targetArgument())
                    .property(
                        QStringLiteral("allow_uicr"),
                        Schema::boolean(QStringLiteral("Also program the image's UICR records "
                                                       "(the PIB is kept)."))
                            .defaultValue(false))
                    .json(),
            .outputSchema = operationOutputSchema(),
            .annotations = destructive,
            .consent = hio::mcp::ConsentCategory::Flash,
            .marker = true,
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
                return std::make_unique<FlashCall>(context, rc, env, std::move(*device), **target,
                                                   args);
            },
            .validate = [](const QJsonObject& args,
                           const hio::mcp::Limits&) -> std::optional<hio::mcp::ValidationError> {
                const bool path = args.contains(QStringLiteral("path"));
                const bool id = args.contains(QStringLiteral("catalog_id"));
                if (path == id) {
                    return invalid(path ? QStringLiteral("catalog_id") : QStringLiteral("path"),
                                   QStringLiteral("exactly one of path and catalog_id is "
                                                  "required"));
                }
                return std::nullopt;
            }});
}

hio::Status addFlashLte(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "flash_lte",
            .title = "Update the LTE modem",
            .description = flashLteDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(
                        QStringLiteral("path"),
                        Schema::string(QStringLiteral("Absolute path to a modem firmware zip, an "
                                                      "LTE bundle zip or a .hex file."))
                            .minLength(1)
                            .maxLength(1024),
                        hio::mcp::Required::Yes)
                    .json(),
            .outputSchema = operationOutputSchema(),
            .annotations = destructive,
            .consent = hio::mcp::ConsentCategory::Flash,
            .marker = true,
            .start = [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                hio::mcp::ToolEnv& env) -> std::unique_ptr<hio::mcp::ToolCall> {
                auto device = resolveJLinkDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                QJsonObject targetArgs = args;
                targetArgs.insert(QStringLiteral("target"), QStringLiteral("lte"));
                auto target = resolveTarget(*device->ref.console, targetArgs);
                if (!target) {
                    return failed(std::move(target).error());
                }
                return std::make_unique<FlashLteCall>(
                    context, rc, env, std::move(*device), **target,
                    args.value(QStringLiteral("path")).toString());
            },
            .validate = {}});
}

hio::Status addErase(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "erase",
            .title = "Erase flash",
            .description = eraseDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("target"), targetArgument())
                    .property(QStringLiteral("scope"),
                              Schema::string(QStringLiteral("\"code\" or \"all\" (default: "
                                                            "the target's usual scope)."))
                                  .enumValues({QStringLiteral("code"), QStringLiteral("all")}))
                    .json(),
            .outputSchema = objectOf({{"scope", typed("string")},
                                      {"target", typed("string")},
                                      {"duration", typed("number")},
                                      {"log_cursor_end", typed("integer")},
                                      {"terminal_cursor_end", typed("integer")}},
                                     {QStringLiteral("scope"), QStringLiteral("duration")}),
            .annotations = destructive,
            .consent = hio::mcp::ConsentCategory::Erase,
            .marker = true,
            .start = operationStart(
                context,
                [](const QJsonObject& args, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits&) -> hio::Result<SimpleOperation> {
                    const std::string id(target.id);
                    const QString requested = args.value(QStringLiteral("scope")).toString();
                    hio::device::EraseScope scope = target.defaultErase;
                    if (!requested.isEmpty()) {
                        scope = requested == QStringLiteral("all") ? hio::device::EraseScope::All
                                                                   : hio::device::EraseScope::Code;
                    }
                    const QString scopeText = scope == hio::device::EraseScope::All
                                                  ? QStringLiteral("all")
                                                  : QStringLiteral("code");
                    QJsonObject details;
                    details.insert(QStringLiteral("target"), qs(id));
                    details.insert(QStringLiteral("scope"), scopeText);
                    // ADR-0161: erasing everything also removes the PIB, which `uicr` protects.
                    hio::mcp::ConsentCategories consent = hio::mcp::ConsentCategory::Erase;
                    if (scope == hio::device::EraseScope::All) {
                        consent |= hio::mcp::ConsentCategory::Uicr;
                    }
                    return SimpleOperation{
                        .settings = programSettings("erase"),
                        .consent = consent,
                        .details = details,
                        .action = QStringLiteral("erase %1 (%2)").arg(qs(id), scopeText),
                        .start = [id, scope](DeviceSession& s) { return s.erase(id, scope); },
                        .result =
                            [id, scopeText](const OperationResult& r) -> hio::Result<QJsonObject> {
                            QJsonObject json;
                            json.insert(QStringLiteral("scope"), scopeText);
                            json.insert(QStringLiteral("target"), qs(id));
                            json.insert(QStringLiteral("duration"), seconds(r.duration));
                            return json;
                        }};
                }),
            .validate = {}});
}

hio::Status addRecover(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "recover",
            .title = "Recover a locked MCU",
            .description = recoverDescription,
            .inputSchema = Schema::object()
                               .property(QStringLiteral("device"), deviceArgument())
                               .property(QStringLiteral("target"), targetArgument())
                               .json(),
            .outputSchema = objectOf({{"target", typed("string")},
                                      {"duration", typed("number")},
                                      {"log_cursor_end", typed("integer")},
                                      {"terminal_cursor_end", typed("integer")}},
                                     {QStringLiteral("duration")}),
            .annotations = destructive,
            // ADR-0161: recover wipes the PIB too (erase + uicr).
            .consent = hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Erase) |
                       hio::mcp::ConsentCategory::Uicr,
            .marker = true,
            .start = operationStart(
                context,
                [](const QJsonObject&, const ResolvedDevice&,
                   const hio::device::TargetProfile& target,
                   const hio::mcp::ToolLimits&) -> hio::Result<SimpleOperation> {
                    const std::string id(target.id);
                    QJsonObject details;
                    details.insert(QStringLiteral("target"), qs(id));
                    return SimpleOperation{
                        .settings = programSettings("recover"),
                        .consent = hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Erase) |
                                   hio::mcp::ConsentCategory::Uicr,
                        .details = details,
                        .action = QStringLiteral("recover %1").arg(qs(id)),
                        .start = [id](DeviceSession& s) { return s.recover(id); },
                        .result = [id](const OperationResult& r) -> hio::Result<QJsonObject> {
                            QJsonObject json;
                            json.insert(QStringLiteral("target"), qs(id));
                            json.insert(QStringLiteral("duration"), seconds(r.duration));
                            return json;
                        }};
                }),
            .validate = {}});
}

} // namespace

hio::Status registerFirmwareTools(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add : {&addFlash, &addFlashLte, &addErase, &addRecover}) {
        if (auto added = add(registry, context); !added) {
            return added;
        }
    }
    return {};
}

} // namespace hio::mcp::tools::detail

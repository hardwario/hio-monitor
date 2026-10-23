#include "mcp_command.h"

#include "chester_commands.h"
#include "cli_app.h"
#include "cli_device_provider.h"
#include "flag_consent_broker.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "hio/mcp/activity.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/http_server.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/server.h"
#include "hio/mcp/stdio_transport.h"
#include "hio/mcp/token.h"
#include "hio/mcp/tools/register_tools.h"
#include "hio/mcp/tools/tools_context.h"
#include "jlink_commands.h"
#include "operation_runner.h"
#include "stdin_line_source.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace hio::cli {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// How long the shutdown waits for cancelled tool calls to answer before the sessions are closed.
constexpr auto drainTimeout = 5s;

const char* const tokenVariable = "HIO_MCP_TOKEN";

// ADR-0106: the client snippets, also shown by the GUI (plan 0014).
const char* const helpFooter =
    "Client configuration examples (ADR-0106):\n"
    "  Claude Code, stdio:\n"
    "    claude mcp add --transport stdio hio -- hio mcp --allow flash\n"
    "  Claude Code, HTTP (start `hio mcp --http` first):\n"
    "    claude mcp add --transport http hio http://127.0.0.1:47800/mcp --header "
    "\"Authorization: Bearer <token>\"\n"
    "  Claude Code .mcp.json (the token stays in the environment):\n"
    "    {\"mcpServers\":{\"hio\":{\"type\":\"http\",\"url\":\"http://127.0.0.1:47800/mcp\","
    "\"headers\":{\"Authorization\":\"Bearer ${HIO_MCP_TOKEN}\"}}}}\n"
    "  Claude Desktop:\n"
    "    {\"mcpServers\":{\"hio\":{\"command\":\"<path>/hio\",\"args\":[\"mcp\"]}}}\n"
    "\n"
    "stdio: the server ends when stdin is closed (end of input); tool calls still running then\n"
    "are cancelled (a running flash is stopped). If writing to stdout fails, the server stops\n"
    "with exit code 130. Requests are queued up to mcp/stdioBufferMiB MiB (settings file,\n"
    "default 16, 4-256); a single request may be up to 4 MiB.";

struct McpOptions {
    bool http = false;
    std::uint16_t port = defaultMcpPort;
    std::string tokenFile;
    bool tokenFileGiven = false;
    std::string allow;
    std::string speed;
    bool speedGiven = false;
    bool autoReconnect = false;
    std::string consoleFileDir;
    bool consoleFileDirGiven = false;
};

fs::path toPath(const std::string& text) { return {toQString(text).toStdU16String()}; }

Error usage(std::string message, std::string detail = {}) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = std::move(message);
    error.cause.clear();
    return error;
}

// ADR-0156: the directory must exist and be writable before anything is served.
Status checkConsoleFileDir(const fs::path& directory) {
    std::error_code ec;
    const std::string shown = toUtf8(directory);
    if (!fs::is_directory(directory, ec)) {
        Error error = makeError(ErrorCode::IoFileNotFound, shown);
        error.message = std::format("The console file directory {} does not exist.", shown);
        error.suggestion = "Create the directory or choose another one with --console-file-dir.";
        return std::unexpected(std::move(error));
    }
    if (!QFileInfo(QString::fromStdU16String(directory.u16string())).isWritable()) {
        Error error = makeError(ErrorCode::IoWriteFailed, shown);
        error.message = std::format("The console file directory {} is not writable.", shown);
        error.suggestion =
            "Check the permissions of the directory or choose another one with --console-file-dir.";
        return std::unexpected(std::move(error));
    }
    return {};
}

enum class TokenSource : std::uint8_t { Environment, File, Generated };

struct Token {
    std::string value;
    TokenSource source = TokenSource::Generated;
    fs::path file;
};

// HIO_MCP_TOKEN > --token-file > a token for this run (plan 0013 "CLI").
Result<Token> resolveToken(const McpOptions& options) {
    const QString environment = qEnvironmentVariable(tokenVariable);
    if (!environment.isEmpty()) {
        std::string value = toStdString(environment);
        if (!hio::mcp::TokenStore::isValidToken(value)) {
            return std::unexpected(usage(std::format("{} must hold a token of 43 base64url "
                                                     "characters (A-Z, a-z, 0-9, - and _).",
                                                     tokenVariable)));
        }
        return Token{.value = std::move(value), .source = TokenSource::Environment, .file = {}};
    }
    if (options.tokenFileGiven) {
        const hio::mcp::TokenStore store(toPath(options.tokenFile));
        auto value = store.loadOrCreate();
        if (!value) {
            return std::unexpected(std::move(value).error());
        }
        return Token{.value = std::move(*value), .source = TokenSource::File, .file = store.path()};
    }
    return Token{.value = hio::mcp::TokenStore::generateToken(),
                 .source = TokenSource::Generated,
                 .file = {}};
}

std::string allowedText(hio::mcp::ConsentCategories allowed) {
    std::string text;
    for (const std::string_view id : hio::mcp::consentCategoryIds(allowed)) {
        text += text.empty() ? std::string(id) : ", " + std::string(id);
    }
    return text.empty() ? std::string("none") : text;
}

// The stderr text of `hio mcp --http` (plan 0013 "User experience"). The full token is printed
// only when it was generated for this run; a token from the environment or a file is not repeated.
void printHttpBanner(std::uint16_t port, const Token& token, hio::mcp::ConsentCategories allowed) {
    std::string text = std::format("MCP server listening on http://127.0.0.1:{}/mcp\n", port);
    switch (token.source) {
    case TokenSource::Generated:
        text += std::format("Token (this run only): {}   — set {} or use --token-file to "
                            "keep one\n",
                            token.value, tokenVariable);
        break;
    case TokenSource::Environment:
        text += std::format("Token: from {}\n", tokenVariable);
        break;
    case TokenSource::File:
        text += std::format("Token: from {}\n", toUtf8(token.file));
        break;
    }
    text += std::format("Allowed destructive tools: {}\n", allowedText(allowed));
    std::cerr << text << std::flush;
}

enum class Ending : std::uint8_t { None, InputClosed, OutputFailed, Stopped };

// The stdin queue budget in bytes (ADR-0163); a warning goes to stderr and the hio.mcp log.
std::size_t stdioBufferBytes() {
    auto settings = Settings::openDefault();
    if (!settings) {
        qCWarning(log::lcMcp).noquote()
            << "Settings not available:" << toQString(settings.error().detail);
    }
    const StdioBufferSetting setting = stdioBufferSetting(settings ? &*settings : nullptr);
    if (setting.warning) {
        qCWarning(log::lcMcp).noquote() << "MCP:" << toQString(*setting.warning);
        std::cerr << "Warning: " << *setting.warning << '\n' << std::flush;
    }
    return static_cast<std::size_t>(setting.mebibytes) * 1024 * 1024;
}

// The checked options of one run (everything that can fail before serving).
struct Prepared {
    hio::mcp::ConsentCategories allowed;
    CliProviderOptions provider;
    std::optional<Token> token; // --http only
};

Result<Prepared> prepare(const GlobalOptions& globals, const McpOptions& options) {
    if (globals.json) {
        return std::unexpected(usage("`mcp` writes JSON-RPC; `--json` does not apply.", "--json"));
    }
    Prepared prepared;
    auto allowed = parseAllowList(options.allow);
    if (!allowed) {
        return std::unexpected(std::move(allowed).error());
    }
    prepared.allowed = *allowed;
    if (options.speedGiven) {
        auto speed = parseJLinkSpeed(options.speed);
        if (!speed) {
            return std::unexpected(std::move(speed).error());
        }
        prepared.provider.speedGiven = true;
        prepared.provider.speedKhz = *speed;
    }
    prepared.provider.autoReconnect = options.autoReconnect;
    if (options.consoleFileDirGiven) {
        const fs::path directory = toPath(options.consoleFileDir);
        if (const Status checked = checkConsoleFileDir(directory); !checked) {
            return std::unexpected(checked.error());
        }
        prepared.provider.consoleFileDir = directory;
    }
    if (!globals.nrfutil.empty()) {
        prepared.provider.nrfUtil = toPath(globals.nrfutil);
    }
    if (options.http) {
        auto resolved = resolveToken(options);
        if (!resolved) {
            return std::unexpected(std::move(resolved).error());
        }
        prepared.token = std::move(*resolved);
    } else if (options.tokenFileGiven || options.port != defaultMcpPort) {
        qCWarning(log::lcMcp) << "MCP: --port and --token-file apply to --http only; ignored";
    }
    return prepared;
}

Result<CommandResult> runMcp(const Context& context, const GlobalOptions& globals,
                             const McpOptions& options) {
    auto prepared = prepare(globals, options);
    if (!prepared) {
        return std::unexpected(std::move(prepared).error());
    }
    const std::optional<Token>& token = prepared->token;
    const hio::mcp::ConsentCategories allowed = prepared->allowed;

    // Owner-thread objects; destroyed in reverse order (the tools context outlives the server,
    // the provider outlives both).
    CliDeviceProvider provider([&globals] { return locateJLinkLibrary(globals); },
                               std::move(prepared->provider));
    FlagConsentBroker consent(allowed);
    LogActivitySink activity;
    firmware::FirmwareCache cache(firmware::FirmwareCache::defaultDirectory());
    firmware::FirmwareCatalog catalog(
        firmware::CatalogOptions{.baseUrl =
                                     firmware::catalogBaseUrl(chesterProfile().firmwareCatalogUrl),
                                 .transferTimeout = std::chrono::seconds(60),
                                 .maxBodyBytes = 32ULL * 1024 * 1024},
        &cache);
    hio::mcp::tools::ToolsContext tools(provider, &catalog);
    hio::mcp::Registry registry;
    if (const Status registered = hio::mcp::tools::registerTools(registry, tools); !registered) {
        return std::unexpected(registered.error());
    }
    hio::mcp::McpServer server(std::move(registry), consent, activity, hio::mcp::ServerOptions{});
    tools.attachServer(server);
    // ADR-0162: the probe enumeration runs on the server's offloader (the provider outlives the
    // server; its QPointer turns null when the offloader is destroyed).
    provider.setOffloader(&server.offloader());

    std::unique_ptr<hio::mcp::HttpServer> http;
    if (token) { // --http
        http = std::make_unique<hio::mcp::HttpServer>(
            server, hio::mcp::HttpServerOptions{.port = options.port, .token = token->value});
        if (const Status listening = http->listen(); !listening) {
            return std::unexpected(listening.error());
        }
        printHttpBanner(http->port(), *token, allowed);
    }

    // stdio: stdout carries only the transport's JSON-RPC lines (nothing else writes to it).
    QFile out;
    std::unique_ptr<StdinLineSource> input;
    std::unique_ptr<hio::mcp::StdioTransport> stdio;
    if (!options.http) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY); // "\n", not "\r\n"
#endif
        if (!out.open(stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
            return std::unexpected(makeError(ErrorCode::IoWriteFailed, "stdout"));
        }
        const hio::mcp::Limits& limits = server.limits();
        // ADR-0163: a line may be up to 4 MiB; the budget (>= 4 MiB) always admits one.
        input = std::make_unique<StdinLineSource>(limits.maxStdioLineBytes, limits.stdinQueueLines,
                                                  stdioBufferBytes());
        stdio = std::make_unique<hio::mcp::StdioTransport>(server, *input, out);
    }
    // One probe enumeration at start (ADR-0162), so list_devices has a list without refresh=true;
    // list_devices calls that arrive while it runs wait for it (review V1, firstScanPending).
    provider.refresh([](const Status& refreshed) {
        if (!refreshed) {
            qCWarning(log::lcMcp).noquote()
                << "MCP: probe enumeration failed:" << toQString(idOf(refreshed.error().code));
        }
    });
    qCInfo(log::lcMcp).noquote() << "MCP server started:"
                                 << (options.http ? QStringLiteral("http")
                                                  : QStringLiteral("stdio"))
                                 << "allowed:" << toQString(allowedText(allowed));

    // Shutdown (ADR-0097): server.stop() cancels every call; once they have answered (at most
    // drainTimeout) the sessions are closed and the event loop ends. Every step runs as a queued
    // call, never inside a session signal (closing a session joins its worker).
    QObject anchor;
    QTimer drainTimer;
    drainTimer.setSingleShot(true);
    drainTimer.setInterval(drainTimeout);
    Ending ending = Ending::None;
    bool finished = false;
    const auto finish = [&] {
        if (finished) {
            return;
        }
        finished = true;
        drainTimer.stop();
        if (http) {
            http->close();
        }
        tools.detachServer();
        provider.closeAll();
        qCInfo(log::lcMcp) << "MCP server stopped";
        QCoreApplication::exit(0);
    };
    const auto beginShutdown = [&](Ending why) {
        if (ending != Ending::None) {
            return;
        }
        ending = why;
        qCInfo(log::lcMcp) << "MCP server stopping, reason" << static_cast<int>(why);
        QObject::connect(&server, &hio::mcp::McpServer::callsDrained, &anchor, finish,
                         Qt::QueuedConnection);
        QObject::connect(&drainTimer, &QTimer::timeout, &anchor, finish);
        drainTimer.start();
        server.stop();
    };
    if (stdio) {
        QObject::connect(
            stdio.get(), &hio::mcp::StdioTransport::inputClosed, &anchor,
            [&beginShutdown] { beginShutdown(Ending::InputClosed); }, Qt::QueuedConnection);
        QObject::connect(
            stdio.get(), &hio::mcp::StdioTransport::outputFailed, &anchor,
            [&beginShutdown] { beginShutdown(Ending::OutputFailed); }, Qt::QueuedConnection);
        stdio->start();
    }
    // Ctrl+C / SIGTERM: SignalGuard requests the stop from its watcher thread; only a queued call
    // crosses to the owner thread. Declared last: destroyed (and waited for) first.
    const std::stop_callback onStop(context.stop, [&anchor, &beginShutdown] {
        QMetaObject::invokeMethod(
            &anchor, [&beginShutdown] { beginShutdown(Ending::Stopped); }, Qt::QueuedConnection);
    });

    (void)QCoreApplication::exec();

    if (ending == Ending::Stopped) {
        return std::unexpected(makeError(ErrorCode::Cancelled));
    }
    if (ending == Ending::OutputFailed) {
        // Review S4: like every other command whose reader went away (exit 130).
        return std::unexpected(outputClosedError("the standard output was closed"));
    }
    return CommandResult{.data = {}, .humanText = {}, .streamed = true};
}

} // namespace

StdioBufferSetting stdioBufferSetting(const Settings* settings) {
    StdioBufferSetting result;
    if (settings == nullptr) {
        return result;
    }
    const std::string raw =
        settings->get(SettingKey<std::string>{.path = stdioBufferKey, .defaultValue = {}});
    if (raw.empty()) {
        return result; // not set
    }
    bool ok = false;
    const qlonglong value = toQString(raw).trimmed().toLongLong(&ok);
    if (!ok) {
        result.warning = std::format("The setting {} = \"{}\" is not a number; using {} MiB.",
                                     stdioBufferKey, raw, defaultStdioBufferMiB);
        return result;
    }
    if (value < minStdioBufferMiB || value > maxStdioBufferMiB) {
        result.mebibytes = value < minStdioBufferMiB ? minStdioBufferMiB : maxStdioBufferMiB;
        result.warning =
            std::format("The setting {} = {} is outside {}-{}; using {} MiB.", stdioBufferKey,
                        value, minStdioBufferMiB, maxStdioBufferMiB, result.mebibytes);
        return result;
    }
    result.mebibytes = static_cast<int>(value);
    return result;
}

void LogActivitySink::record(hio::mcp::ActivityEntry entry) {
    qCInfo(log::lcMcp).noquote() << "MCP call" << toQString(entry.tool) << "client"
                                 << toQString(entry.client) << "device"
                                 << (entry.deviceId.empty() ? QStringLiteral("-")
                                                            : toQString(entry.deviceId))
                                 << "result" << toQString(entry.result) << "in"
                                 << entry.duration.count() << "ms; arguments"
                                 << toQString(entry.arguments);
}

void registerMcpCommand(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals) {
    auto options = std::make_shared<McpOptions>();
    CLI::App* mcp = app.add_subcommand(
        "mcp", "Run the MCP server for AI assistants (stdio, or HTTP with --http)");
    // The snippets are printed as written (no paragraph reflow); otherwise the default format.
    auto formatter = std::make_shared<CLI::Formatter>();
    formatter->enable_footer_formatting(false);
    mcp->formatter(formatter);
    mcp->footer(helpFooter);
    mcp->add_flag("--http", options->http,
                  "Serve MCP over HTTP on 127.0.0.1 instead of stdin/stdout (ends with Ctrl+C)");
    mcp->add_option("--port", options->port, "HTTP port on 127.0.0.1 (--http; default 47800)")
        ->capture_default_str()
        ->check(CLI::Range(minMcpPort, maxMcpPort))
        ->option_text("N");
    CLI::Option* tokenFile =
        mcp->add_option("--token-file", options->tokenFile,
                        "HTTP bearer token file (--http): read the token, or create the file with "
                        "a new one; $HIO_MCP_TOKEN wins. Default: a token for this run only")
            ->option_text("PATH");
    mcp->add_option("--allow", options->allow,
                    "Destructive tools to allow: comma-separated flash, erase, memory-write, uicr "
                    "or all (default: none)")
        ->option_text("LIST");
    CLI::Option* speed =
        mcp->add_option("--jlink-speed", options->speed,
                        "J-Link speed for connect: auto or kHz (default: the target's default)")
            ->option_text("K");
    mcp->add_flag("--auto-reconnect", options->autoReconnect,
                  "connect keeps re-attaching after the link drops (default for connect)");
    CLI::Option* consoleFileDir =
        mcp->add_option("--console-file-dir", options->consoleFileDir,
                        "Directory for the console files of the sessions (must exist; default: "
                        "the application data)")
            ->option_text("DIR");
    mcp->callback([&slot, &globals, options, tokenFile, speed, consoleFileDir] {
        options->tokenFileGiven = tokenFile->count() > 0;
        options->speedGiven = speed->count() > 0;
        options->consoleFileDirGiven = consoleFileDir->count() > 0;
        slot.command = [&globals, options](const Context& context) {
            return runMcp(context, globals, *options);
        };
    });
}

} // namespace hio::cli

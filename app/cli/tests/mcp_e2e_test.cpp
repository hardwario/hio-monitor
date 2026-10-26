// End-to-end tests of `hio mcp` (plan/features/0013 C6, "CLI e2e"): the real `hio` executable with
// the fake J-Link library, driven over stdio with scripted JSON-RPC in both dialects (legacy
// 2025-11-25 and modern 2026-07-28) and over HTTP with a Qt client.

#include "cli_process.h"
#include "hex_builder.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QHostAddress>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtContainerFwd>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#endif

namespace {

using namespace std::chrono_literals;
using hio::cli::test::CliProcess;
using hio::cli::test::RunResult;

constexpr const char* device = "jlink:801000001";
// A valid token (43 base64url characters); a test value only.
constexpr const char* testToken = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

constexpr const char* scenario = R"({"probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "booted\n"}]}}]})";

// Processes events until `done` or the timeout.
bool runUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const QDeadlineTimer deadline(timeout);
    while (!done()) {
        if (deadline.hasExpired()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

QJsonObject modernMeta() {
    return QJsonObject{
        {QStringLiteral("io.modelcontextprotocol/protocolVersion"), QStringLiteral("2026-07-28")},
        {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject()},
        {QStringLiteral("io.modelcontextprotocol/clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("e2e")},
                     {QStringLiteral("version"), QStringLiteral("1")}}}};
}

QJsonObject message(int id, const QString& method, QJsonObject params, bool modern) {
    if (modern) {
        params.insert(QStringLiteral("_meta"), modernMeta());
    }
    QJsonObject result{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                       {QStringLiteral("method"), method},
                       {QStringLiteral("params"), params}};
    if (id >= 0) {
        result.insert(QStringLiteral("id"), id);
    }
    return result;
}

QJsonObject toolCall(int id, const QString& tool, const QJsonObject& arguments, bool modern) {
    return message(
        id, QStringLiteral("tools/call"),
        QJsonObject{{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}},
        modern);
}

QJsonObject structured(const QJsonObject& response) {
    return response.value(QStringLiteral("result"))
        .toObject()
        .value(QStringLiteral("structuredContent"))
        .toObject();
}

bool isToolError(const QJsonObject& response) {
    return response.value(QStringLiteral("result"))
        .toObject()
        .value(QStringLiteral("isError"))
        .toBool();
}

QString toolErrorCode(const QJsonObject& response) {
    return structured(response)
        .value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
        .toString();
}

// One running `hio mcp` child over stdio.
class McpProcess {
public:
    McpProcess(const QStringList& args, const QProcessEnvironment& env)
        : m_started(start(m_process, args, env)) {}
    ~McpProcess() {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(10000);
        }
    }
    McpProcess(const McpProcess&) = delete;
    McpProcess& operator=(const McpProcess&) = delete;
    McpProcess(McpProcess&&) = delete;
    McpProcess& operator=(McpProcess&&) = delete;

    [[nodiscard]] bool started() const { return m_started; }

    void send(const QJsonObject& object) {
        m_process.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    }

    // The response with this id (other messages, e.g. notifications, are kept in lines()).
    [[nodiscard]] std::optional<QJsonObject> response(int id,
                                                      std::chrono::milliseconds timeout = 30s) {
        std::optional<QJsonObject> found;
        (void)runUntil(
            [&] {
                read();
                for (const QJsonObject& line : m_messages) {
                    if (line.value(QStringLiteral("id")) == QJsonValue(id)) {
                        found = line;
                        return true;
                    }
                }
                return m_process.state() == QProcess::NotRunning;
            },
            timeout);
        return found;
    }

    [[nodiscard]] QJsonObject request(const QJsonObject& object) {
        send(object);
        return response(object.value(QStringLiteral("id")).toInt()).value_or(QJsonObject());
    }

    void sendRaw(const QByteArray& bytes) { m_process.write(bytes); }

    void closeStdin() { m_process.closeWriteChannel(); }

#ifndef _WIN32
    void interrupt() { ::kill(static_cast<int>(m_process.processId()), SIGINT); }
#endif

    // Waits for the exit; the exit code, or -1 when it hung (then killed) or crashed.
    [[nodiscard]] int finish(std::chrono::milliseconds timeout = 30s) {
        const bool exited =
            runUntil([this] { return m_process.state() == QProcess::NotRunning; }, timeout);
        read();
        if (!exited) {
            m_process.kill();
            m_process.waitForFinished(10000);
            return -1;
        }
        return m_process.exitStatus() == QProcess::NormalExit ? m_process.exitCode() : -1;
    }

    // Every complete stdout line so far; each must be a JSON-RPC 2.0 object.
    [[nodiscard]] const std::vector<QByteArray>& lines() const { return m_lines; }
    [[nodiscard]] std::string err() {
        m_err += m_process.readAllStandardError();
        m_err.replace("\r\n", "\n"); // stderr is a text-mode stream ("\r\n" on Windows)
        return m_err.toStdString();
    }

private:
    static bool start(QProcess& process, const QStringList& args, const QProcessEnvironment& env) {
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(HIO_CLI_EXECUTABLE),
                      QStringList{QStringLiteral("mcp")} << args);
        return process.waitForStarted(10000);
    }

    void read() {
        m_buffer += m_process.readAllStandardOutput();
        qsizetype end = 0;
        while ((end = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(end);
            m_buffer.remove(0, end + 1);
            m_lines.push_back(line);
            m_messages.push_back(QJsonDocument::fromJson(line).object());
        }
        m_err += m_process.readAllStandardError();
        m_err.replace("\r\n", "\n");
    }

    QProcess m_process;
    bool m_started;
    QByteArray m_buffer;
    QByteArray m_err;
    std::vector<QByteArray> m_lines;
    std::vector<QJsonObject> m_messages;
};

void expectOnlyJsonRpc(const std::vector<QByteArray>& lines) {
    for (const QByteArray& line : lines) {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        EXPECT_EQ(error.error, QJsonParseError::NoError) << line.toStdString();
        EXPECT_TRUE(document.isObject()) << line.toStdString();
        EXPECT_EQ(document.object().value(QStringLiteral("jsonrpc")).toString(),
                  QStringLiteral("2.0"))
            << line.toStdString();
    }
}

// A port nobody listens on right now (chosen by the OS).
quint16 freePort() {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return server.serverPort();
}

class McpCli : public CliProcess {
protected:
    void SetUp() override {
        writeFile(QStringLiteral("scenario.json"), scenario);
        writeFile(QStringLiteral("app.hex"),
                  QByteArray::fromStdString(
                      hio::firmware::test::hexRecord(0, 0x2000, {1, 2, 3, 4, 5, 6, 7, 8}) +
                      hio::firmware::test::eofRecord()));
    }

    void writeFile(const QString& name, const QByteArray& bytes) const {
        QFile file(path(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }
    [[nodiscard]] QString path(const QString& name) const {
        return home() + QLatin1Char('/') + name;
    }

    [[nodiscard]] QProcessEnvironment fakeEnvironment() const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HIO_FAKE_JLINK_SCENARIO"),
                   path(QStringLiteral("scenario.json")));
        env.insert(QStringLiteral("HIO_JLINK_LIBRARY"), QStringLiteral(HIO_FAKE_JLINK_LIBRARY));
        env.remove(QStringLiteral("HIO_MCP_TOKEN"));
        return env;
    }

    // `hio jlink list`: nobody (no session, no other process) uses the probe any more.
    void expectProbeFree() const {
        const RunResult list =
            run({QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("list")},
                fakeEnvironment());
        ASSERT_EQ(list.exitCode, 0) << list.out << list.err;
        const QJsonArray probes = QJsonDocument::fromJson(QByteArray::fromStdString(list.out))
                                      .object()
                                      .value(QStringLiteral("data"))
                                      .toObject()
                                      .value(QStringLiteral("probes"))
                                      .toArray();
        ASSERT_EQ(probes.size(), 1);
        EXPECT_TRUE(probes.at(0).toObject().value(QStringLiteral("inUseBy")).isNull());
    }

    // connect + wait until the console is connected.
    static void connectDevice(McpProcess& mcp, int& id, bool modern) {
        const QJsonObject connected = mcp.request(toolCall(
            id++, QStringLiteral("connect"), {{QStringLiteral("device"), device}}, modern));
        ASSERT_FALSE(isToolError(connected)) << QJsonDocument(connected).toJson().toStdString();
        EXPECT_EQ(structured(connected).value(QStringLiteral("device")).toString(),
                  QString::fromLatin1(device));
        const QJsonObject waited = mcp.request(toolCall(id++, QStringLiteral("wait_for_connection"),
                                                        {{QStringLiteral("timeout"), 10}}, modern));
        ASSERT_TRUE(structured(waited).value(QStringLiteral("connected")).toBool())
            << QJsonDocument(waited).toJson().toStdString();
    }
};

TEST_F(McpCli, LegacyStdioConnectFlashStatusAndEof) {
    McpProcess mcp({QStringLiteral("--allow"), QStringLiteral("flash")}, fakeEnvironment());
    ASSERT_TRUE(mcp.started());
    int id = 1;
    const QJsonObject init = mcp.request(
        message(id++, QStringLiteral("initialize"),
                QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-11-25")},
                            {QStringLiteral("capabilities"), QJsonObject()},
                            {QStringLiteral("clientInfo"),
                             QJsonObject{{QStringLiteral("name"), QStringLiteral("e2e")},
                                         {QStringLiteral("version"), QStringLiteral("1")}}}},
                false));
    EXPECT_EQ(init.value(QStringLiteral("result"))
                  .toObject()
                  .value(QStringLiteral("protocolVersion"))
                  .toString(),
              QStringLiteral("2025-11-25"));
    mcp.send(message(-1, QStringLiteral("notifications/initialized"), {}, false));

    const QJsonObject list = mcp.request(message(id++, QStringLiteral("tools/list"), {}, false));
    QStringList names;
    for (const auto& tool :
         list.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray()) {
        names.append(tool.toObject().value(QStringLiteral("name")).toString());
    }
    EXPECT_TRUE(names.contains(QStringLiteral("connect")));
    EXPECT_TRUE(names.contains(QStringLiteral("flash")));

    // Review V1: no refresh=true needed; the call waits for the start-up enumeration.
    const QJsonObject devices =
        mcp.request(toolCall(id++, QStringLiteral("list_devices"), {}, false));
    const QJsonArray entries = structured(devices).value(QStringLiteral("devices")).toArray();
    ASSERT_EQ(entries.size(), 1) << QJsonDocument(devices).toJson().toStdString();
    EXPECT_EQ(entries.at(0).toObject().value(QStringLiteral("state")).toString(),
              QStringLiteral("available"));

    connectDevice(mcp, id, false);
    const QJsonObject flash =
        mcp.request(toolCall(id++, QStringLiteral("flash"),
                             {{QStringLiteral("path"), path(QStringLiteral("app.hex"))}}, false));
    ASSERT_FALSE(isToolError(flash)) << QJsonDocument(flash).toJson().toStdString();
    EXPECT_EQ(structured(flash).value(QStringLiteral("status")).toString(), QStringLiteral("ok"));

    const QJsonObject status = mcp.request(toolCall(id++, QStringLiteral("status"), {}, false));
    const QJsonArray open = structured(status).value(QStringLiteral("devices")).toArray();
    ASSERT_EQ(open.size(), 1) << QJsonDocument(status).toJson().toStdString();
    EXPECT_EQ(open.at(0).toObject().value(QStringLiteral("id")).toString(),
              QString::fromLatin1(device));

    mcp.closeStdin();
    EXPECT_EQ(mcp.finish(), 0) << mcp.err();
    expectOnlyJsonRpc(mcp.lines());
    // No startup text on stderr in stdio mode (ADR-0106: warnings only).
    EXPECT_EQ(mcp.err().find("MCP server listening"), std::string::npos);
    expectProbeFree();
}

TEST_F(McpCli, ModernStdioWithoutAllowAndConsoleFileDir) {
    const QString consoleDir = path(QStringLiteral("console-files"));
    ASSERT_TRUE(QDir().mkpath(consoleDir));
    McpProcess mcp({QStringLiteral("--console-file-dir"), consoleDir}, fakeEnvironment());
    ASSERT_TRUE(mcp.started());
    int id = 1;
    const QJsonObject discover =
        mcp.request(message(id++, QStringLiteral("server/discover"), {}, true));
    EXPECT_TRUE(discover.contains(QStringLiteral("result")))
        << QJsonDocument(discover).toJson().toStdString();
    const QJsonObject list = mcp.request(message(id++, QStringLiteral("tools/list"), {}, true));
    EXPECT_FALSE(list.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("tools"))
                     .toArray()
                     .isEmpty());

    connectDevice(mcp, id, true);
    const QJsonObject flash =
        mcp.request(toolCall(id++, QStringLiteral("flash"),
                             {{QStringLiteral("path"), path(QStringLiteral("app.hex"))}}, true));
    ASSERT_TRUE(isToolError(flash)) << QJsonDocument(flash).toJson().toStdString();
    EXPECT_EQ(toolErrorCode(flash), QStringLiteral("mcp.tool_not_allowed"));
    const QJsonObject error = structured(flash).value(QStringLiteral("error")).toObject();
    EXPECT_EQ(error.value(QStringLiteral("message")).toString(),
              QStringLiteral("The tool flash is not allowed by this server."));
    EXPECT_EQ(error.value(QStringLiteral("suggestion")).toString(),
              QStringLiteral("Start the server with --allow flash (or --allow all)."));

    const QJsonObject status = mcp.request(
        toolCall(id++, QStringLiteral("status"), {{QStringLiteral("device"), device}}, true));
    EXPECT_FALSE(isToolError(status)) << QJsonDocument(status).toJson().toStdString();

    mcp.closeStdin();
    EXPECT_EQ(mcp.finish(), 0) << mcp.err();
    expectOnlyJsonRpc(mcp.lines());
    // ADR-0156: the session's console file went into the directory.
    EXPECT_FALSE(QDir(consoleDir).entryList(QDir::Files).isEmpty());
    expectProbeFree();
}

TEST_F(McpCli, OverlongAndInvalidLinesAreParseErrors) {
    McpProcess mcp({}, fakeEnvironment());
    ASSERT_TRUE(mcp.started());
    mcp.sendRaw("not json\n");
    // A line over the 4 MiB limit is dropped and answered with a parse error (ADR-0102).
    QByteArray overlong((4 * 1024 * 1024) + 16, 'x');
    overlong.append('\n');
    mcp.sendRaw(overlong);
    // The server keeps serving afterwards.
    mcp.send(message(7, QStringLiteral("tools/list"), {}, true));
    ASSERT_TRUE(mcp.response(7).has_value()) << mcp.err();
    mcp.closeStdin();
    EXPECT_EQ(mcp.finish(), 0) << mcp.err();
    expectOnlyJsonRpc(mcp.lines());
    int parseErrors = 0;
    for (const QByteArray& line : mcp.lines()) {
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.value(QStringLiteral("error"))
                .toObject()
                .value(QStringLiteral("code"))
                .toInt() == -32700) {
            ++parseErrors;
        }
    }
    EXPECT_EQ(parseErrors, 2);
}

TEST_F(McpCli, JsonFlagIsAUsageError) {
    const RunResult result =
        run({QStringLiteral("--json"), QStringLiteral("mcp")}, fakeEnvironment());
    EXPECT_EQ(result.exitCode, 2);
    const QJsonObject envelope =
        QJsonDocument::fromJson(QByteArray::fromStdString(result.out)).object();
    const QJsonObject error = envelope.value(QStringLiteral("error")).toObject();
    EXPECT_EQ(error.value(QStringLiteral("code")).toString(),
              QStringLiteral("usage.invalid_argument"));
    EXPECT_EQ(error.value(QStringLiteral("message")).toString(),
              QStringLiteral("`mcp` writes JSON-RPC; `--json` does not apply."));
}

TEST_F(McpCli, UnknownAllowItemIsAUsageError) {
    const RunResult result =
        run({QStringLiteral("mcp"), QStringLiteral("--allow"), QStringLiteral("flash,format")},
            fakeEnvironment());
    EXPECT_EQ(result.exitCode, 2);
    EXPECT_TRUE(result.out.empty());
    EXPECT_NE(result.err.find("Unknown --allow item \"format\""), std::string::npos) << result.err;
}

TEST_F(McpCli, MissingConsoleFileDirFailsBeforeServing) {
    const RunResult result = run({QStringLiteral("mcp"), QStringLiteral("--console-file-dir"),
                                  path(QStringLiteral("missing"))},
                                 fakeEnvironment(),
                                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                                 "\"tools/list\",\"params\":{}}\n");
    EXPECT_EQ(result.exitCode, 3);
    EXPECT_TRUE(result.out.empty()) << result.out; // nothing was served
    EXPECT_NE(result.err.find("does not exist"), std::string::npos) << result.err;
}

#ifndef _WIN32
TEST_F(McpCli, SigintEndsWith130AndClosesSessions) {
    McpProcess mcp({}, fakeEnvironment());
    ASSERT_TRUE(mcp.started());
    int id = 1;
    connectDevice(mcp, id, true);
    // A long wait is running when Ctrl+C arrives: it is cancelled, the process does not hang.
    mcp.send(toolCall(id++, QStringLiteral("wait_for_state"),
                      {{QStringLiteral("pattern"), QStringLiteral("never-printed")},
                       {QStringLiteral("timeout"), 600}},
                      true));
    QThread::msleep(200);
    mcp.interrupt();
    EXPECT_EQ(mcp.finish(), 130) << mcp.err();
    expectOnlyJsonRpc(mcp.lines());
    expectProbeFree();
}
#endif

#ifndef _WIN32
// Review S4: a failed write to stdout (the client closed its end) ends `hio mcp` like any command
// whose reader went away: `cancelled` "The output was closed.", exit 130. stdin stays open.
TEST_F(McpCli, ClosedStdoutEndsWith130) {
    QProcess shell;
    shell.setProcessEnvironment(fakeEnvironment());
    shell.start(QStringLiteral("bash"),
                {QStringLiteral("-c"),
                 QStringLiteral(R"("$0" mcp | true; exit "${PIPESTATUS[0]}")"),
                 QStringLiteral(HIO_CLI_EXECUTABLE)});
    ASSERT_TRUE(shell.waitForStarted(10000));
    // Requests until a response write fails (the first ones may still fit the pipe buffer).
    const QByteArray request = QJsonDocument(message(1, QStringLiteral("tools/list"), {}, true))
                                   .toJson(QJsonDocument::Compact) +
                               '\n';
    const bool exited = runUntil(
        [&] {
            if (shell.state() == QProcess::NotRunning) {
                return true;
            }
            shell.write(request);
            QThread::msleep(20);
            return false;
        },
        30s);
    if (!exited) {
        shell.kill();
        shell.waitForFinished(5000);
    }
    ASSERT_TRUE(exited) << "hio mcp did not stop after its stdout was closed";
    const std::string err = shell.readAllStandardError().toStdString();
    EXPECT_EQ(shell.exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(shell.exitCode(), 130) << err;
    EXPECT_NE(err.find("Error: The output was closed."), std::string::npos) << err;
}
#endif

// Review S5: `hio mcp --help` documents that stdin EOF cancels the running calls.
TEST_F(McpCli, HelpDocumentsTheEndOfInput) {
    const RunResult help =
        run({QStringLiteral("mcp"), QStringLiteral("--help")}, fakeEnvironment());
    EXPECT_EQ(help.exitCode, 0);
    const std::string text = help.out + help.err;
    EXPECT_NE(text.find("tool calls still running then"), std::string::npos) << text;
    EXPECT_NE(text.find("are cancelled"), std::string::npos) << text;
    EXPECT_NE(text.find("mcp/stdioBufferMiB"), std::string::npos) << text;
}

// ADR-0163: an out-of-range mcp/stdioBufferMiB is clamped with a warning on stderr.
#ifdef __linux__
TEST_F(McpCli, StdioBufferSettingIsClampedWithAWarning) {
    ASSERT_TRUE(QDir().mkpath(home() + QStringLiteral("/config/HARDWARIO")));
    writeFile(QStringLiteral("config/HARDWARIO/Monitor.ini"),
              "[meta]\nschemaVersion=1\n[mcp]\nstdioBufferMiB=1000\n");
    McpProcess mcp({}, fakeEnvironment());
    ASSERT_TRUE(mcp.started());
    mcp.send(message(3, QStringLiteral("tools/list"), {}, true));
    ASSERT_TRUE(mcp.response(3).has_value()) << mcp.err();
    mcp.closeStdin();
    EXPECT_EQ(mcp.finish(), 0) << mcp.err();
    EXPECT_NE(mcp.err().find("Warning: The setting mcp/stdioBufferMiB = 1000 is outside 4-256; "
                             "using 256 MiB."),
              std::string::npos)
        << mcp.err();
}
#endif

// ---- HTTP -----------------------------------------------------------------------------------

class McpHttpCli : public McpCli {
protected:
    struct Reply {
        int status = 0;
        QByteArray body;
    };

    // One POST /mcp over a raw QTcpSocket (no QNetworkAccessManager: its HTTP thread is
    // uninstrumented Qt code, so these tests also run under TSan).
    [[nodiscard]] static Reply post(quint16 port, const QJsonObject& body, const QByteArray& token,
                                    const QByteArray& name = {}) {
        const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
        QByteArray request = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
                             "\r\nContent-Type: application/json\r\n"
                             "Accept: application/json, text/event-stream\r\n"
                             "Authorization: Bearer " +
                             token + "\r\nMCP-Protocol-Version: 2026-07-28\r\nMcp-Method: " +
                             body.value(QStringLiteral("method")).toString().toUtf8() + "\r\n";
        if (!name.isEmpty()) {
            request += "Mcp-Name: " + name + "\r\n";
        }
        request += "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        Reply result;
        if (!socket.waitForConnected(10000)) {
            return result;
        }
        socket.write(request);
        QByteArray response;
        // The status line, the headers and a Content-Length body (the server answers JSON here).
        const auto complete = [&response] {
            const qsizetype headerEnd = response.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return false;
            }
            const QByteArray headers = response.left(headerEnd).toLower();
            const qsizetype at = headers.indexOf("content-length:");
            if (at < 0) {
                return true;
            }
            const qsizetype lineEnd = headers.indexOf("\r\n", at);
            const qsizetype length =
                headers.mid(at + 15, lineEnd < 0 ? -1 : lineEnd - at - 15).trimmed().toLongLong();
            return response.size() >= headerEnd + 4 + length;
        };
        while (!complete() && socket.state() == QAbstractSocket::ConnectedState) {
            if (!socket.waitForReadyRead(20000)) {
                break;
            }
            response += socket.readAll();
        }
        response += socket.readAll();
        const qsizetype headerEnd = response.indexOf("\r\n\r\n");
        const QList<QByteArray> statusLine = response.left(response.indexOf("\r\n")).split(' ');
        result.status = statusLine.size() > 1 ? statusLine.at(1).toInt() : 0;
        result.body = headerEnd < 0 ? QByteArray() : response.mid(headerEnd + 4);
        return result;
    }

    // Waits for the banner on stderr.
    [[nodiscard]] static bool waitForBanner(McpProcess& mcp) {
        return runUntil(
            [&mcp] { return mcp.err().find("Allowed destructive tools:") != std::string::npos; },
            20s);
    }
};

TEST_F(McpHttpCli, ToolsOverHttpWithTokenFromTheEnvironment) {
    const quint16 port = freePort();
    ASSERT_NE(port, 0);
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HIO_MCP_TOKEN"), QString::fromLatin1(testToken));
    McpProcess mcp({QStringLiteral("--http"), QStringLiteral("--port"), QString::number(port),
                    QStringLiteral("--allow"), QStringLiteral("erase,uicr")},
                   env);
    ASSERT_TRUE(mcp.started());
    ASSERT_TRUE(waitForBanner(mcp)) << mcp.err();
    const std::string banner = mcp.err();
    EXPECT_NE(banner.find(QStringLiteral("MCP server listening on http://127.0.0.1:%1/mcp\n")
                              .arg(port)
                              .toStdString()),
              std::string::npos)
        << banner;
    EXPECT_NE(banner.find("Token: from HIO_MCP_TOKEN\n"), std::string::npos) << banner;
    EXPECT_NE(banner.find("Allowed destructive tools: erase, uicr\n"), std::string::npos) << banner;
    EXPECT_EQ(banner.find(testToken), std::string::npos); // the token is not repeated

    const Reply list = post(port, message(1, QStringLiteral("tools/list"), {}, true), testToken);
    ASSERT_EQ(list.status, 200) << list.body.toStdString();
    EXPECT_FALSE(QJsonDocument::fromJson(list.body)
                     .object()
                     .value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("tools"))
                     .toArray()
                     .isEmpty());
    // Review V1: without refresh=true, right after start: waits for the start-up enumeration.
    const Reply devices = post(port, toolCall(2, QStringLiteral("list_devices"), {}, true),
                               testToken, "list_devices");
    ASSERT_EQ(devices.status, 200) << devices.body.toStdString();
    EXPECT_EQ(structured(QJsonDocument::fromJson(devices.body).object())
                  .value(QStringLiteral("devices"))
                  .toArray()
                  .size(),
              1);
    const Reply refused = post(port, message(3, QStringLiteral("tools/list"), {}, true), "wrong");
    EXPECT_EQ(refused.status, 401);

#ifdef _WIN32
    // No SIGINT on Windows; the exit code is not checked there.
    mcp.closeStdin();
#else
    mcp.interrupt();
    EXPECT_EQ(mcp.finish(), 130) << mcp.err();
    EXPECT_TRUE(mcp.lines().empty()); // HTTP mode never writes to stdout
#endif
}

TEST_F(McpHttpCli, TokenFileIsCreatedAndGeneratedTokenIsPrinted) {
    const quint16 port = freePort();
    ASSERT_NE(port, 0);
    const QString tokenFile = path(QStringLiteral("tokens/mcp-token"));
    ASSERT_TRUE(QDir().mkpath(path(QStringLiteral("tokens"))));
#ifndef _WIN32
    // Review R2: a directory the user chose is never changed and must not be writable by group or
    // others. mkpath() follows the umask (often 002, group-writable): refused first, then fixed.
    ASSERT_TRUE(QFile::setPermissions(path(QStringLiteral("tokens")),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                          QFileDevice::WriteGroup | QFileDevice::ExeGroup));
    {
        const RunResult refused =
            run({QStringLiteral("mcp"), QStringLiteral("--http"), QStringLiteral("--port"),
                 QString::number(port), QStringLiteral("--token-file"), tokenFile},
                fakeEnvironment());
        EXPECT_NE(refused.exitCode, 0);
        EXPECT_NE(refused.err.find("The MCP token file cannot be read or created."),
                  std::string::npos)
            << refused.err;
        EXPECT_FALSE(QFile::exists(tokenFile));
    }
    ASSERT_TRUE(QFile::setPermissions(path(QStringLiteral("tokens")), QFileDevice::ReadOwner |
                                                                          QFileDevice::WriteOwner |
                                                                          QFileDevice::ExeOwner));
#endif
    {
        McpProcess mcp({QStringLiteral("--http"), QStringLiteral("--port"), QString::number(port),
                        QStringLiteral("--token-file"), tokenFile},
                       fakeEnvironment());
        ASSERT_TRUE(mcp.started());
        ASSERT_TRUE(waitForBanner(mcp)) << mcp.err();
        EXPECT_NE(mcp.err().find("Token: from " + tokenFile.toStdString() + "\n"),
                  std::string::npos)
            << mcp.err();
        EXPECT_NE(mcp.err().find("Allowed destructive tools: none\n"), std::string::npos);
        QFile file(tokenFile);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QByteArray token = file.readLine().trimmed();
        EXPECT_EQ(token.size(), 43);
#ifndef _WIN32
        EXPECT_EQ(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                  QFileDevice::Permissions());
#endif
        const Reply list = post(port, message(1, QStringLiteral("tools/list"), {}, true), token);
        EXPECT_EQ(list.status, 200) << list.body.toStdString();
    }
    {
        // Without a file or HIO_MCP_TOKEN: a token for this run, printed once.
        McpProcess mcp({QStringLiteral("--http"), QStringLiteral("--port"), QString::number(port)},
                       fakeEnvironment());
        ASSERT_TRUE(mcp.started());
        ASSERT_TRUE(waitForBanner(mcp)) << mcp.err();
        const std::string err = mcp.err();
        const std::string prefix = "Token (this run only): ";
        const auto at = err.find(prefix);
        ASSERT_NE(at, std::string::npos) << err;
        const std::string token = err.substr(at + prefix.size(), 43);
        EXPECT_NE(err.find("set HIO_MCP_TOKEN or use --token-file to keep one"), std::string::npos);
        const Reply list = post(port, message(1, QStringLiteral("tools/list"), {}, true),
                                QByteArray::fromStdString(token));
        EXPECT_EQ(list.status, 200) << list.body.toStdString();
    }
}

TEST_F(McpHttpCli, PortInUseExitsWith3) {
    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, 0));
    const RunResult result = run({QStringLiteral("mcp"), QStringLiteral("--http"),
                                  QStringLiteral("--port"), QString::number(blocker.serverPort())},
                                 fakeEnvironment());
    EXPECT_EQ(result.exitCode, 3) << result.err;
    EXPECT_NE(result.err.find("The MCP port is already in use."), std::string::npos) << result.err;
    EXPECT_EQ(result.err.find("MCP server listening"), std::string::npos);
}

TEST_F(McpHttpCli, PortOutOfRangeIsAUsageError) {
    const RunResult result =
        run({QStringLiteral("mcp"), QStringLiteral("--http"), QStringLiteral("--port"), "80"},
            fakeEnvironment());
    EXPECT_EQ(result.exitCode, 2) << result.err;
}

TEST_F(McpHttpCli, InvalidTokenInTheEnvironmentIsAUsageError) {
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HIO_MCP_TOKEN"), QStringLiteral("short"));
    const RunResult result = run({QStringLiteral("mcp"), QStringLiteral("--http"),
                                  QStringLiteral("--port"), QString::number(freePort())},
                                 env);
    EXPECT_EQ(result.exitCode, 2) << result.err;
    EXPECT_NE(result.err.find("HIO_MCP_TOKEN must hold a token"), std::string::npos) << result.err;
}

} // namespace

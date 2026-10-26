#pragma once

// Runs the real `hio` executable (HIO_CLI_EXECUTABLE) with QProcess in private config/data
// directories (shared by the end-to-end tests).

#include <QByteArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>

namespace hio::cli::test {

struct RunResult {
    int exitCode = -1;
    bool normalExit = false;
    std::string out;
    std::string err;
};

class CliProcess : public ::testing::Test {
protected:
    // Every child gets private config/data directories, so tests never write the user's files.
    [[nodiscard]] QProcessEnvironment environment() const {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString root = m_home.path();
        env.insert(QStringLiteral("HOME"), root);
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), root + QStringLiteral("/config"));
        env.insert(QStringLiteral("XDG_DATA_HOME"), root + QStringLiteral("/data"));
        env.insert(QStringLiteral("APPDATA"), root + QStringLiteral("/appdata"));
        env.insert(QStringLiteral("LOCALAPPDATA"), root + QStringLiteral("/localappdata"));
        env.remove(QStringLiteral("NO_COLOR"));
        return env;
    }

    // `input` is written to the child's stdin, which is then closed.
    [[nodiscard]] static RunResult run(const QStringList& args, const QProcessEnvironment& env,
                                       const QByteArray& input = {}) {
        QProcess process;
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(HIO_CLI_EXECUTABLE), args);
        RunResult result;
        if (!process.waitForStarted(10000)) {
            return result;
        }
        process.write(input);
        process.closeWriteChannel();
        if (!process.waitForFinished(60000)) {
            process.kill();
            process.waitForFinished();
            // Keep what the hung process printed (diagnosis); exitCode stays -1.
            result.out = process.readAllStandardOutput().toStdString();
            result.err = "[killed after 60 s] " + process.readAllStandardError().toStdString();
            return result;
        }
        result.normalExit = process.exitStatus() == QProcess::NormalExit;
        result.exitCode = process.exitCode();
        result.out = process.readAllStandardOutput().toStdString();
        result.err = process.readAllStandardError().toStdString();
        normalizeLineEnds(result);
        return result;
    }

    // Human output goes through text-mode streams, which end lines with "\r\n" on Windows; the
    // tests compare "\n" (`--json` output is the same either way after this).
    static void normalizeLineEnds(RunResult& result) {
        for (std::string* text : {&result.out, &result.err}) {
            std::string normalized;
            normalized.reserve(text->size());
            for (std::size_t i = 0; i < text->size(); ++i) {
                if ((*text)[i] != '\r' || i + 1 >= text->size() || (*text)[i + 1] != '\n') {
                    normalized += (*text)[i];
                }
            }
            *text = std::move(normalized);
        }
    }

    [[nodiscard]] RunResult run(const QStringList& args) const { return run(args, environment()); }

    [[nodiscard]] QString home() const { return m_home.path(); }

    [[nodiscard]] QString logDir() const {
        return m_home.path() + QStringLiteral("/data/HARDWARIO/Monitor/logs");
    }

private:
    QTemporaryDir m_home;
};

} // namespace hio::cli::test

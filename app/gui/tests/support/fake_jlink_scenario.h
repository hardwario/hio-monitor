#pragma once

// The fake J-Link library (tests/fake_jlink) for the GUI tests: HIO_JLINK_LIBRARY points the
// locator at it and HIO_FAKE_JLINK_SCENARIO at a scenario file in a temporary directory.

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QtEnvironmentVariables>

namespace hio::gui::test {

// Two probes with RTT shells (the second one powered and answering after 0 ms).
inline constexpr const char* twoProbesScenario = R"({
  "libraryVersion": 81000,
  "probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
     "listFirmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted A\n"}]}},
    {"serialNumber": 801000002, "product": "Test Probe 2",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted B\n"}]}}
  ]})";

// Writes `json` to <directory>/scenario.json and sets both environment variables. False when the
// file cannot be written.
inline bool useFakeJLink(const QString& directory, const char* json) {
    const QString path = directory + QStringLiteral("/scenario.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(json);
    file.close();
    qputenv("HIO_FAKE_JLINK_SCENARIO", path.toUtf8());
    qputenv("HIO_JLINK_LIBRARY", QByteArray(HIO_FAKE_JLINK_LIBRARY));
    return true;
}

} // namespace hio::gui::test

// Test-only stand-in for Nordic's nRF Util (plan/features/0003-m2-flashing.md, D2). Built as an
// executable named `nrfutil` in its own directory; never installed or shipped.
//
//   nrfutil --version                 "nrfutil 7.13.0 (fake)"
//   nrfutil device --version          "nrfutil-device 2.8.1 (fake)"
//   nrfutil device program --firmware <zip> --serial-number <sn> --json
//                                     NDJSON progress lines and a task_end result
//
// Behaviour from the environment:
//   HIO_FAKE_NRFUTIL_MODE  ok (default) | fail | hang | flood | nodevice | old
//   HIO_FAKE_NRFUTIL_LOG   a file; every invocation appends its arguments as one line
//   HIO_FAKE_NRFUTIL_STEP_MS  delay between progress lines (default 20)

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string environment(const char* name) {
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): single-threaded tool
    return value == nullptr ? std::string() : std::string(value);
}

void progressLine(int percent) {
    std::cout << R"json({"type":"task_progress","data":{"task":{"id":"1","name":"program"},)json"
              << R"json("progress":{"progressPercentage":)json" << percent
              << R"json(,"description":"Programming"}}})json" << '\n'
              << std::flush;
}

int program(const std::string& mode) {
    const std::string stepText = environment("HIO_FAKE_NRFUTIL_STEP_MS");
    const auto step = std::chrono::milliseconds(stepText.empty() ? 20 : std::stoi(stepText));
    std::cout << R"json({"type":"info","data":{"message":"fake nrfutil"}})json" << '\n';
    if (mode == "flood") {
        const std::string junk(1024, 'x');
        for (int i = 0; i < 2048; ++i) { // 2 MiB on each stream
            std::cout << junk << '\n';
            std::cerr << junk << '\n';
        }
    }
    for (int percent = 0; percent <= 100; percent += 25) {
        progressLine(percent);
        std::this_thread::sleep_for(step);
        if (mode == "hang" && percent == 25) {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        if (mode == "fail" && percent == 50) {
            std::cout
                << R"json({"type":"task_end","data":{"task":{"id":"1"},"result":"fail","message":"Modem DFU failed (simulated)"}})json"
                << '\n'
                << std::flush;
            std::cerr << "Error: Modem DFU failed (simulated)\n";
            return 1;
        }
    }
    std::cout << R"json({"type":"task_end","data":{"task":{"id":"1"},"result":"success"}})json"
              << '\n'
              << std::flush;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
    const std::vector<std::string> args(raw.begin() + 1, raw.end());
    const std::string mode = environment("HIO_FAKE_NRFUTIL_MODE");
    if (const std::string log = environment("HIO_FAKE_NRFUTIL_LOG"); !log.empty()) {
        std::ofstream file(log, std::ios::app);
        for (const std::string& arg : args) {
            file << arg << ' ';
        }
        file << '\n';
    }
    if (args.size() == 1 && args[0] == "--version") {
        std::cout << (mode == "old" ? "nrfutil 6.1.0 (fake)\n" : "nrfutil 7.13.0 (fake)\n");
        return 0;
    }
    if (args.size() == 2 && args[0] == "device" && args[1] == "--version") {
        if (mode == "nodevice") {
            std::cerr
                << "Error: the command 'device' is not installed. Run 'nrfutil install device'.\n";
            return 1;
        }
        std::cout << "nrfutil-device 2.8.1 (fake)\n";
        return 0;
    }
    if (args.size() >= 2 && args[0] == "device" && args[1] == "program") {
        return program(mode);
    }
    std::cerr << "fake nrfutil: unsupported arguments\n";
    return 2;
}

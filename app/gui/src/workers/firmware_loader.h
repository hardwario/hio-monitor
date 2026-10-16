#pragma once

// FirmwareLoader - firmware parsing and dependency detection off the GUI thread
// (plan/features/0006-m3-flash-settings.md, § Data flow and threading; ADR-0040).
//
// One owned std::jthread for the application (owned by FirmwareServices). It
//   - loads a firmware file: Intel HEX, LTE bundle zip or modem firmware zip, classified by content
//     (firmware::classifyLteInput, ADR-0040; bundles are extracted into a temporary directory that
//     lives as long as the LteBundle);
//   - detects the J-Link library (locate, then load an own copy to read its version: ADR-0030,
//     one-shot, unloaded at once) and nRF Util (locate, then the version probes, up to 10 s each;
//     a stop request kills a running probe, so it never delays quitting).
// Images are immutable and cross threads as std::shared_ptr<const ...>.
//
// Requests go into a bounded queue (maxQueued = 4). A new request of the same requester and kind
// replaces its pending one (the newest wins; the replaced one never gets a result: its requester
// waits for the newer generation). When the queue is full the oldest pending request is dropped and
// answered at once with internal.queue_full under its own generation, so its requester never stays
// in Loading / Checking. Every request has a generation (counts from 1); results carry it and the
// requester drops stale ones. Results are handed over in a mutex-protected list (bounded by
// maxResults; at most one result per request) and the GUI thread is woken with one coalesced queued
// call by name (the DiagWorker pattern); the signals are emitted there. The destructor stops and
// joins the thread (a running HEX parse or library load finishes first; an nRF Util version probe
// is killed).
//
// Thread safety: the public functions are called on the GUI thread (the owner).

#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/nrfutil/nrfutil.h"

#include <QObject>
#include <QtTypes>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <variant>

namespace hio::gui {

// What a loaded firmware file is (ADR-0040 classes).
enum class SourceClass : std::uint8_t {
    IntelHex,  // an application image (app or lte MCU)
    LteBundle, // zip with modem.zip + application.hex
    ModemZip,  // Nordic modem firmware zip (mfw_nrf91*.zip)
};

struct LoadedSource {
    SourceClass sourceClass = SourceClass::IntelHex;
    std::filesystem::path path;  // the file ("" for a catalog download)
    std::uint64_t fileBytes = 0; // size of the file / the HEX text
    // IntelHex: the image; LteBundle: the bundle's application image; ModemZip: null.
    std::shared_ptr<const firmware::FirmwareImage> image;
    std::shared_ptr<const firmware::LteBundle> bundle; // LteBundle only
};

struct JLinkFound {
    std::filesystem::path path;
    jlink::LibrarySource source = jlink::LibrarySource::Discovered;
    std::uint32_t version = 0; // read from the loaded library
};

struct NrfUtilStatus {
    std::optional<std::filesystem::path> executable; // located (also when the versions failed)
    Result<nrfutil::NrfUtilInfo> info;
};

class FirmwareLoader : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t maxQueued = 4;
    // Undelivered results: the worker's (at most maxQueued + 1 between two deliveries) plus the
    // answers to dropped requests. The oldest is discarded (and logged) beyond it.
    static constexpr std::size_t maxResults = 4 * maxQueued;

    explicit FirmwareLoader(std::filesystem::path instanceRoot, QObject* parent = nullptr);
    ~FirmwareLoader() override; // stops and joins the worker
    FirmwareLoader(const FirmwareLoader&) = delete;
    FirmwareLoader& operator=(const FirmwareLoader&) = delete;
    FirmwareLoader(FirmwareLoader&&) = delete;
    FirmwareLoader& operator=(FirmwareLoader&&) = delete;

    // Each returns the request's generation. `requester` only identifies whose pending request a
    // new one replaces (never dereferenced).
    std::uint64_t loadFile(const void* requester, std::filesystem::path path);
    std::uint64_t checkJLink(const void* requester, jlink::LocatorOptions options);
    std::uint64_t checkNrfUtil(const void* requester, nrfutil::LocatorOptions options);

signals:
    void fileLoaded(quint64 generation, const hio::Result<hio::gui::LoadedSource>& result);
    void jlinkChecked(quint64 generation, const hio::Result<hio::gui::JLinkFound>& result);
    void nrfUtilChecked(quint64 generation, const hio::gui::NrfUtilStatus& status);

private:
    struct LoadFile {
        std::filesystem::path path;
    };
    struct CheckJLink {
        jlink::LocatorOptions options;
    };
    struct CheckNrfUtil {
        nrfutil::LocatorOptions options;
    };
    using Payload = std::variant<LoadFile, CheckJLink, CheckNrfUtil>;
    struct Request {
        std::uint64_t generation = 0;
        const void* requester = nullptr;
        Payload payload;
    };
    struct Loaded {
        std::uint64_t generation = 0;
        Result<LoadedSource> result;
    };
    struct JLinkDone {
        std::uint64_t generation = 0;
        Result<JLinkFound> result;
    };
    struct NrfUtilDone {
        std::uint64_t generation = 0;
        NrfUtilStatus status;
    };
    using Outcome = std::variant<Loaded, JLinkDone, NrfUtilDone>;

    // GUI thread: invoked (queued, by name) by the worker.
    Q_INVOKABLE void deliver();

    std::uint64_t enqueue(const void* requester, Payload payload);
    void run(const std::stop_token& stop);
    [[nodiscard]] Outcome process(Request& request, const std::stop_token& stop) const;
    // The answer to a request dropped from the full queue (internal.queue_full).
    [[nodiscard]] static Outcome dropped(const Request& request);
    void post(Outcome outcome);
    void pushResult(Outcome outcome); // m_mutex held

    const std::filesystem::path m_instanceRoot;
    std::mutex m_mutex; // guards the members below
    std::condition_variable_any m_wake;
    std::deque<Request> m_queue;   // bounded: maxQueued
    std::deque<Outcome> m_results; // bounded: maxResults
    std::uint64_t m_nextGeneration = 1;

    std::atomic<bool> m_deliverPending{false};
    std::jthread m_worker; // last member: started last, joined first
};

// Loads and classifies `path` (the worker's step; exposed for tests). Errors: those of
// firmware::classifyLteInput (io.*, firmware.invalid_image, firmware.hex_parse_error).
[[nodiscard]] Result<LoadedSource> loadFirmwareFile(const std::filesystem::path& path);

} // namespace hio::gui

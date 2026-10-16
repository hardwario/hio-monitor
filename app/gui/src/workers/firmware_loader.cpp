#include "workers/firmware_loader.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/probe_enumerator.h"
#include "hio/hw/nrfutil/nrfutil.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace hio::gui {

namespace {

namespace fs = std::filesystem;

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

std::uint64_t fileSize(const fs::path& path) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    return error ? 0 : static_cast<std::uint64_t>(size);
}

} // namespace

Result<LoadedSource> loadFirmwareFile(const fs::path& path) {
    auto input = firmware::classifyLteInput(path);
    if (!input) {
        return std::unexpected(std::move(input).error());
    }
    LoadedSource loaded{.sourceClass = SourceClass::IntelHex,
                        .path = path,
                        .fileBytes = fileSize(path),
                        .image = nullptr,
                        .bundle = nullptr};
    std::visit(Overloaded{
                   [&loaded](firmware::AppHex& hex) {
                       loaded.sourceClass = SourceClass::IntelHex;
                       loaded.image =
                           std::make_shared<const firmware::FirmwareImage>(std::move(hex.image));
                   },
                   [&loaded](firmware::LteBundle& bundle) {
                       loaded.sourceClass = SourceClass::LteBundle;
                       auto shared = std::make_shared<const firmware::LteBundle>(std::move(bundle));
                       // The application image shares the bundle's lifetime (aliasing pointer).
                       loaded.image = std::shared_ptr<const firmware::FirmwareImage>(
                           shared, &shared->application);
                       loaded.bundle = std::move(shared);
                   },
                   [&loaded](firmware::ModemOnly& modem) {
                       loaded.sourceClass = SourceClass::ModemZip;
                       loaded.path = std::move(modem.path);
                   },
               },
               *input);
    return loaded;
}

FirmwareLoader::FirmwareLoader(fs::path instanceRoot, QObject* parent)
    : QObject(parent), m_instanceRoot(std::move(instanceRoot)),
      m_worker([this](const std::stop_token& stop) { run(stop); }) {}

FirmwareLoader::~FirmwareLoader() {
    m_worker.request_stop();
    m_wake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

std::uint64_t FirmwareLoader::loadFile(const void* requester, fs::path path) {
    return enqueue(requester, LoadFile{.path = std::move(path)});
}

std::uint64_t FirmwareLoader::checkJLink(const void* requester, jlink::LocatorOptions options) {
    return enqueue(requester, CheckJLink{.options = std::move(options)});
}

std::uint64_t FirmwareLoader::checkNrfUtil(const void* requester, nrfutil::LocatorOptions options) {
    return enqueue(requester, CheckNrfUtil{.options = std::move(options)});
}

std::uint64_t FirmwareLoader::enqueue(const void* requester, Payload payload) {
    std::uint64_t generation = 0;
    bool dropAnswered = false;
    {
        const std::scoped_lock lock(m_mutex);
        generation = m_nextGeneration++;
        Request request{
            .generation = generation, .requester = requester, .payload = std::move(payload)};
        // The newest request replaces a pending one of the same requester and kind.
        const auto same = std::ranges::find_if(m_queue, [&request](const Request& queued) {
            return queued.requester == request.requester &&
                   queued.payload.index() == request.payload.index();
        });
        if (same != m_queue.end()) {
            *same = std::move(request);
        } else {
            if (m_queue.size() >= maxQueued) {
                qCWarning(lcGui) << "Firmware loader queue full: request"
                                 << m_queue.front().generation << "dropped";
                pushResult(dropped(m_queue.front()));
                dropAnswered = true;
                m_queue.pop_front();
            }
            m_queue.push_back(std::move(request));
        }
    }
    m_wake.notify_all();
    if (dropAnswered && !m_deliverPending.exchange(true)) {
        QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection);
    }
    return generation;
}

FirmwareLoader::Outcome FirmwareLoader::dropped(const Request& request) {
    const auto error = [&request] {
        return makeError(ErrorCode::InternalQueueFull, "firmware loader queue full: request " +
                                                           std::to_string(request.generation) +
                                                           " dropped");
    };
    return std::visit(
        Overloaded{
            [&](const LoadFile&) -> Outcome {
                return Loaded{.generation = request.generation, .result = std::unexpected(error())};
            },
            [&](const CheckJLink&) -> Outcome {
                return JLinkDone{.generation = request.generation,
                                 .result = std::unexpected(error())};
            },
            [&](const CheckNrfUtil&) -> Outcome {
                return NrfUtilDone{.generation = request.generation,
                                   .status = NrfUtilStatus{.executable = std::nullopt,
                                                           .info = std::unexpected(error())}};
            },
        },
        request.payload);
}

void FirmwareLoader::run(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        Request request;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return !m_queue.empty(); })) {
                break;
            }
            request = std::move(m_queue.front());
            m_queue.pop_front();
        }
        Outcome outcome = process(request, stop);
        if (stop.stop_requested()) {
            break;
        }
        post(std::move(outcome));
    }
}

FirmwareLoader::Outcome FirmwareLoader::process(Request& request,
                                                const std::stop_token& stop) const {
    const std::uint64_t generation = request.generation;
    return std::visit(
        Overloaded{
            [generation](LoadFile& load) -> Outcome {
                return Loaded{.generation = generation, .result = loadFirmwareFile(load.path)};
            },
            [this, generation](CheckJLink& check) -> Outcome {
                auto location = jlink::locateLibrary(check.options);
                if (!location) {
                    return JLinkDone{.generation = generation,
                                     .result = std::unexpected(std::move(location).error())};
                }
                // ADR-0030: an own copy, loaded only to read the version and unloaded at once.
                auto enumerator = jlink::ProbeEnumerator::load(*location, m_instanceRoot);
                if (!enumerator) {
                    return JLinkDone{.generation = generation,
                                     .result = std::unexpected(std::move(enumerator).error())};
                }
                return JLinkDone{.generation = generation,
                                 .result = JLinkFound{.path = location->path,
                                                      .source = location->source,
                                                      .version = enumerator->libraryVersion()}};
            },
            [generation, &stop](CheckNrfUtil& check) -> Outcome {
                auto executable = nrfutil::locate(check.options);
                if (!executable) {
                    return NrfUtilDone{.generation = generation,
                                       .status = NrfUtilStatus{
                                           .executable = std::nullopt,
                                           .info = std::unexpected(std::move(executable).error())}};
                }
                return NrfUtilDone{
                    .generation = generation,
                    .status = NrfUtilStatus{.executable = *executable,
                                            .info = nrfutil::checkVersions(
                                                *executable, std::chrono::seconds(10), stop)}};
            },
        },
        request.payload);
}

void FirmwareLoader::pushResult(Outcome outcome) {
    // At most one result per request; the bound is a safety net.
    if (m_results.size() >= maxResults) {
        qCWarning(lcGui) << "Firmware loader: undelivered result discarded";
        m_results.pop_front();
    }
    m_results.push_back(std::move(outcome));
}

void FirmwareLoader::post(Outcome outcome) {
    {
        const std::scoped_lock lock(m_mutex);
        pushResult(std::move(outcome));
    }
    if (!m_deliverPending.exchange(true)) {
        // By name: Qt creates and consumes the queued call (see DeviceSession::wakeUp).
        QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection);
    }
}

void FirmwareLoader::deliver() {
    m_deliverPending.store(false);
    std::deque<Outcome> results;
    {
        const std::scoped_lock lock(m_mutex);
        results.swap(m_results);
    }
    for (Outcome& outcome : results) {
        std::visit(
            Overloaded{
                [this](Loaded& item) { emit fileLoaded(item.generation, item.result); },
                [this](JLinkDone& item) { emit jlinkChecked(item.generation, item.result); },
                [this](NrfUtilDone& item) { emit nrfUtilChecked(item.generation, item.status); },
            },
            outcome);
    }
}

} // namespace hio::gui

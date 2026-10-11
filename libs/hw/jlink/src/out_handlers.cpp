#include "out_handlers.h"

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "jlink_abi.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace hio::jlink {

namespace {

using Queue = BoundedQueue<LibraryMessage>;
using Kind = LibraryMessage::Kind;

// Process-wide table of slots. Owner: OutHandlerSlot (acquire / destructor under `mutex`);
// the handlers only touch the entries of their own slot.
//
// Teardown protocol (review fix 6): a handler increments inFlight, then loads the queue pointer,
// pushes, and decrements inFlight. The destructor stores nullptr into the queue pointer, then
// waits until inFlight is 0. With sequentially consistent operations either the handler's
// increment comes first (the destructor waits for it) or the destructor's store comes first (the
// handler sees nullptr), so no handler can use a destroyed queue - also when a vendor thread is
// inside a handler during teardown.
struct SlotTable {
    std::mutex mutex; // guards acquire/release (the queue pointer stores)
    std::array<std::atomic<Queue*>, OutHandlerSlot::maxSlots> queues{};
    std::array<std::atomic<int>, OutHandlerSlot::maxSlots> inFlight{};
};

// Decrements inFlight when a handler leaves (also on an exception).
class InFlightGuard {
public:
    explicit InFlightGuard(std::atomic<int>& counter) noexcept : m_counter(&counter) {
        m_counter->fetch_add(1, std::memory_order_seq_cst);
    }
    ~InFlightGuard() { m_counter->fetch_sub(1, std::memory_order_seq_cst); }
    InFlightGuard(const InFlightGuard&) = delete;
    InFlightGuard& operator=(const InFlightGuard&) = delete;
    InFlightGuard(InFlightGuard&&) = delete;
    InFlightGuard& operator=(InFlightGuard&&) = delete;

private:
    std::atomic<int>* m_counter;
};

SlotTable& slotTable() {
    static SlotTable table;
    return table;
}

// Messages lost because copying them failed (out of memory), all slots together.
std::atomic<std::uint64_t>& lostMessageCount() {
    static std::atomic<std::uint64_t> count{0};
    return count;
}

// The vendor text up to its NUL, at most `maxBytes` bytes (never reads further), cut back to a
// UTF-8 character boundary when truncated.
std::string_view boundedText(const char* text, std::size_t maxBytes) noexcept {
    std::size_t length = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C callback argument
    while (length < maxBytes && text[length] != '\0') {
        ++length;
    }
    std::string_view view(text, length);
    if (length == maxBytes) {
        while (!view.empty() && (static_cast<unsigned char>(view.back()) & 0xC0U) == 0x80U) {
            view.remove_suffix(1);
        }
        if (!view.empty() && (static_cast<unsigned char>(view.back()) & 0x80U) != 0U) {
            view.remove_suffix(1); // the lead byte of the cut character
        }
    }
    return view;
}

// The handler of slot `Slot` for messages of kind `K`. Called by the vendor library on any of its
// threads; never throws, never touches Qt objects.
template <std::size_t Slot, Kind K> void onLibraryText(const char* text) noexcept {
    try {
        SlotTable& table = slotTable();
        const InFlightGuard inFlight(table.inFlight.at(Slot));
        Queue* const queue = table.queues.at(Slot).load(std::memory_order_seq_cst);
        if (queue == nullptr || text == nullptr) {
            return;
        }
        LibraryMessage message;
        message.kind = K;
        message.text = std::string(boundedText(text, OutHandlerSlot::maxMessageBytes));
        if (!queue->push(std::move(message))) {
            return; // closed (DropOldest never rejects otherwise)
        }
    } catch (...) {
        // Out of memory while copying: the message is lost (a C callback must not throw); the
        // owner logs the count (OutHandlerSlot::lostMessages).
        lostMessageCount().fetch_add(1, std::memory_order_relaxed);
    }
}

template <Kind K, std::size_t... Slots>
constexpr std::array<abi::LogHandler, sizeof...(Slots)>
makeHandlers(std::index_sequence<Slots...> /*slots*/) {
    return {&onLibraryText<Slots, K>...};
}

constexpr auto logHandlers =
    makeHandlers<Kind::Log>(std::make_index_sequence<OutHandlerSlot::maxSlots>{});
constexpr auto warningHandlers =
    makeHandlers<Kind::Warning>(std::make_index_sequence<OutHandlerSlot::maxSlots>{});
constexpr auto errorHandlers =
    makeHandlers<Kind::Error>(std::make_index_sequence<OutHandlerSlot::maxSlots>{});

} // namespace

Result<std::unique_ptr<OutHandlerSlot>> OutHandlerSlot::acquire() {
    SlotTable& table = slotTable();
    const std::scoped_lock lock(table.mutex);
    for (std::size_t index = 0; index < table.queues.size(); ++index) {
        if (table.queues.at(index).load(std::memory_order_relaxed) == nullptr) {
            auto slot = std::make_unique<OutHandlerSlot>(Passkey{}, index);
            table.queues.at(index).store(&slot->m_queue, std::memory_order_release);
            return slot;
        }
    }
    return std::unexpected(
        makeError(ErrorCode::InternalSessionLimit,
                  std::format("all {} J-Link library out-handler slots are in use", maxSlots)));
}

OutHandlerSlot::OutHandlerSlot(Passkey /*key*/, std::size_t index)
    : m_index(index), m_queue(maxQueuedMessages, OverflowPolicy::DropOldest) {}

OutHandlerSlot::~OutHandlerSlot() {
    SlotTable& table = slotTable();
    const std::scoped_lock lock(table.mutex);
    table.queues.at(m_index).store(nullptr, std::memory_order_seq_cst);
    // A handler that saw the queue pointer before the store finishes its push first (bounded:
    // one push into a DropOldest queue).
    while (table.inFlight.at(m_index).load(std::memory_order_seq_cst) != 0) {
        std::this_thread::yield();
    }
}

std::uint64_t OutHandlerSlot::lostMessages() noexcept {
    return lostMessageCount().load(std::memory_order_relaxed);
}

abi::LogHandler OutHandlerSlot::logHandler() const noexcept { return logHandlers.at(m_index); }

abi::LogHandler OutHandlerSlot::warningHandler() const noexcept {
    return warningHandlers.at(m_index);
}

abi::LogHandler OutHandlerSlot::errorHandler() const noexcept { return errorHandlers.at(m_index); }

} // namespace hio::jlink

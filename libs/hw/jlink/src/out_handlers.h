#pragma once

// Log / warning / error out handlers of the JLinkARM library (plan/features/0002-m1-jlink-rtt.md).
//
// The library calls plain C function pointers without a user-data argument, and every loaded copy
// (ADR-0030) needs its own handlers. A fixed table of maxSlots slots provides distinct handler
// functions per slot; each loaded copy holds one slot (OutHandlerSlot, RAII) for its lifetime.
//
// The handlers are noexcept, never touch Qt objects (CLAUDE.md), and only copy the text (truncated
// at maxMessageBytes) into the slot's bounded queue (maxQueuedMessages, DropOldest). The owner of
// the library copy drains the queue on its own thread (JLinkLibrary::flushMessages).
//
// Bounds: maxSlots slots per process (8 sessions + short-lived enumerations; acquire() fails with
// internal.session_limit when all are used), 256 queued messages per slot, 1 KiB per message.
// Thread safety: the slot table is guarded by a mutex; handlers read their slot's queue pointer
// atomically. A slot is released only after its library copy was unloaded, so no handler of that
// copy can still run.

#include "hio/core/bounded_queue.h"
#include "hio/core/result.h"
#include "jlink_abi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace hio::jlink {

struct LibraryMessage {
    enum class Kind : std::uint8_t {
        Log,
        Warning,
        Error,
    };
    Kind kind = Kind::Log;
    std::string text;
};

class OutHandlerSlot {
    struct Passkey {
        explicit Passkey() = default;
    };

public:
    static constexpr std::size_t maxSlots = 16;
    static constexpr std::size_t maxQueuedMessages = 256;
    static constexpr std::size_t maxMessageBytes = 1024;

    // Errors: internal.session_limit when all slots are in use.
    [[nodiscard]] static Result<std::unique_ptr<OutHandlerSlot>> acquire();

    OutHandlerSlot(Passkey key, std::size_t index);
    ~OutHandlerSlot();
    OutHandlerSlot(const OutHandlerSlot&) = delete;
    OutHandlerSlot& operator=(const OutHandlerSlot&) = delete;
    OutHandlerSlot(OutHandlerSlot&&) = delete;
    OutHandlerSlot& operator=(OutHandlerSlot&&) = delete;

    // Messages lost in any slot because copying them failed (out of memory).
    [[nodiscard]] static std::uint64_t lostMessages() noexcept;

    [[nodiscard]] abi::LogHandler logHandler() const noexcept;
    [[nodiscard]] abi::LogHandler warningHandler() const noexcept;
    [[nodiscard]] abi::LogHandler errorHandler() const noexcept;

    // Owner thread of the library copy.
    [[nodiscard]] BoundedQueue<LibraryMessage>& queue() noexcept { return m_queue; }

private:
    std::size_t m_index;
    BoundedQueue<LibraryMessage> m_queue;
};

} // namespace hio::jlink

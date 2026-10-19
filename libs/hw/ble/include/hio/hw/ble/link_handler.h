#pragma once

// ILinkHandler - what a console link does with its data (ADR-0129). libs/hw/ble only moves bytes;
// the owner of a link (libs/device BleSession) supplies a handler with the Connect command. The
// handler is then owned by the BLE thread and called only there (line assembly, prompt detection
// and the console file run there, ADR-0128). Every call must be short and must not throw.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/types.h"

#include <cstddef>
#include <span>
#include <string>

namespace hio::ble {

class ILinkHandler {
public:
    ILinkHandler() = default;
    ILinkHandler(const ILinkHandler&) = delete;
    ILinkHandler& operator=(const ILinkHandler&) = delete;
    ILinkHandler(ILinkHandler&&) = delete;
    ILinkHandler& operator=(ILinkHandler&&) = delete;
    virtual ~ILinkHandler() = default;

    // A connect attempt starts; `automatic` = a retry the owner asked for without the user.
    virtual void onConnecting(bool automatic) = 0;
    // The CCCD write has been pending for the prompt delay: the OS asks for the passkey now.
    virtual void onPairingPrompt() = 0;
    // TX notifications are enabled: the link is usable.
    virtual void onReady(LinkReady ready) = 0;
    // Notified bytes from the device, in order.
    virtual void onData(std::span<const std::byte> data) = 0;
    // A line was written completely (the echo of the command, ADR-0082).
    virtual void onLineWritten(std::string line) = 0;
    // A line was not (completely) written: not connected, queue full, too long, or the link went
    // down or was dropped while it waited or was being written. Each sendLine() ends with exactly
    // one onLineWritten() or onLineRejected().
    virtual void onLineRejected(std::string line, hio::Error error) = 0;
    // The connect attempt failed or the connected link went down.
    virtual void onDown(hio::Error error) = 0;
    // The result of a pairing request for this link's device (BleSession::pair()).
    virtual void onPairingFinished(Status status, bool alreadyPaired) = 0;
    // Periodic (<= 250 ms) while the link exists: flush buffered output (console file).
    virtual void onFlush() = 0;
    // The link is closed for good (session closed or the host stops); the handler is destroyed
    // right after this call.
    virtual void onClosed() = 0;
};

} // namespace hio::ble

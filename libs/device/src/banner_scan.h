#pragma once

// BannerScan - the boot banner fallback of the console file name (plan/features/0028 D10,
// ADR-0200), shared by the J-Link session worker and the BLE console handler (the threads that
// own the console file, ADR-0128). Per attach it looks at the first maxLines output lines within
// window after Connected for an application banner (identityFromBanner()); a hit is used only when
// the owner reported that `info show` gave no identity for that attach (an `info show` identity
// always wins). Thread safety: none; owned by one worker / handler.

#include "hwio/device/firmware_identity.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hwio::device::detail {

class BannerScan {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    static constexpr std::size_t maxLines = 200;
    static constexpr std::chrono::seconds window{5};

    // A new attach (Connected): returns its number (1, 2, ...).
    std::uint64_t attached(TimePoint now) {
        ++m_attach;
        m_since = now;
        m_lines = 0;
        m_banner.reset();
        m_infoMissing = false;
        m_infoGiven = false;
        return m_attach;
    }
    [[nodiscard]] std::uint64_t attach() const noexcept { return m_attach; }

    // An output line of the current attach; the banner identity to adopt now (when the owner
    // already said `info show` gave none), else nullopt.
    [[nodiscard]] std::optional<FirmwareIdentity> feed(std::string_view text, TimePoint now) {
        if (m_attach == 0 || m_banner || m_lines >= maxLines || now - m_since > window) {
            return std::nullopt;
        }
        ++m_lines;
        m_banner = identityFromBanner(text);
        return m_infoMissing && !m_infoGiven ? m_banner : std::nullopt;
    }

    // The owner's identity for `attach` (ConsoleSession::setFirmwareIdentity): what to adopt
    // now; nullopt for a stale attach or when nothing is known.
    [[nodiscard]] std::optional<FirmwareIdentity>
    answer(const std::optional<FirmwareIdentity>& identity, std::uint64_t attach) {
        if (attach != m_attach || m_attach == 0) {
            return std::nullopt;
        }
        if (identity) {
            m_infoGiven = true; // an `info show` identity always wins
            return identity;
        }
        if (m_infoGiven) {
            return std::nullopt; // e.g. a refused later re-query of the same attach
        }
        m_infoMissing = true;
        return m_banner;
    }

private:
    std::uint64_t m_attach = 0;
    TimePoint m_since;
    std::size_t m_lines = 0;
    std::optional<FirmwareIdentity> m_banner;
    bool m_infoMissing = false;
    bool m_infoGiven = false;
};

} // namespace hwio::device::detail

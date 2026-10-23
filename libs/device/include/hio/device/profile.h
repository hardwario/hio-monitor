#pragma once

// Device profiles: compiled-in C++ data (ADR-0032). A profile describes a device and its targets
// (MCUs): J-Link device names, RTT buffer names, console rules, defaults and (M2,
// plan/features/0003) the memory layout and identification used for flashing. Profiles contain
// data only; behaviour differences are explicit typed fields (e.g. hashPrefixIsLog).
//
// Adding a device = one new file in src/profiles/ + one registration line in profile.cpp + tests.

#include "hio/core/result.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_image.h"
#include "hio/hw/jlink/programmer.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hio::device {

// A part number found in FICR INFO.PART and what it means for this target.
struct ChipPart {
    std::uint32_t part = 0;       // e.g. 0x52840
    std::string_view displayName; // "nRF52840"
    std::string_view jlinkDevice; // J-Link device name of exactly this part
};

// What `recover` does after the CTRL-AP erase-all (ADR-0039).
enum class RecoverImage : std::uint8_t {
    None,
    Nrf91DisableApProtect, // program + verify the embedded nrf91_disable_ap_protect.hex
};

enum class EraseScope : std::uint8_t {
    Code, // the code flash (UICR and the PIB kept)
    All,  // flash and UICR (the PIB is lost)
};

// One step of the cabling guide of a target (ADR-0077), shown by the Connect and Flash wizards
// (plan/features/0009, 0010). Texts are English source texts in context "hio::device"
// (QT_TRANSLATE_NOOP), translated by the GUI; the illustration is a slot id of
// app/gui/assets/wizard (ADR-0076), never an image (libs/device has no QtGui).
struct ConnectionGuideStep {
    std::string_view id;               // kebab-case, unique within the target ("cable")
    std::string_view illustrationSlot; // e.g. "chester-app-cable"
    const char* titleSourceText = "";  // short step title ("Cable")
    const char* bodySourceText = "";   // one or two sentences
};

struct TargetProfile {
    std::string_view id; // "app", "lte" (CLI, logs, file names)
    // In user-visible text ("the %1 MCU", "the %1 connector"): English source text, marked with
    // QT_TRANSLATE_NOOP in context "hio::device" and shown through targetDisplayName().
    std::string_view displayName; // "application", "LTE"
    std::string_view jlinkDevice; // J-Link device name used to connect, e.g. "NRF52840_xxAA"
    std::string_view rttTerminal; // RTT shell buffer name ("" = no RTT console)
    std::string_view rttLogger;   // RTT log buffer name ("" = none)
    bool hashPrefixIsLog;         // shell lines starting with '#' are log lines (older CHESTER fw)
    // J-Link speed in kHz; nullopt = automatic (JLINKARM_SetSpeed(0), as pylink; ADR-0090).
    std::optional<int> defaultSpeedKhz;
    int defaultLatencyMs;
    int minTargetMillivolts; // below this VTref the target counts as unpowered

    // ---- flashing (M2) ----
    std::span<const ChipPart> parts; // accepted parts; the first one is the default
    jlink::ChipFamily chipFamily;    // FICR and CTRL-AP of this MCU family
    firmware::AddressRange flash;    // code flash
    firmware::AddressRange uicr;     // UICR page
    std::uint32_t pibOffset;         // Product Information Block: UICR + pibOffset, 128 B
    bool pibErasableWithUicr;        // a UICR erase also erases the PIB (nRF52: yes)
    RecoverImage recoverImage;
    EraseScope defaultErase; // `erase` without --all (app: Code, lte: All)

    // ---- wizards (M4, ADR-0077) ----
    std::span<const ConnectionGuideStep> connectionGuide; // cabling steps, in order

    // ---- low-level target operations (M6, plan/features/0013, ADR-0099, ADR-0160) ----
    // Half-open ranges. Memory reads may touch any 32-bit range that does not wrap (an unmapped
    // address fails in the backend). Memory writes only inside `ram` or one `peripherals` range
    // and never overlapping a `writeDenied` range (the flash controller: its ERASEALL /
    // ERASEUICR / ERASEPAGE tasks would wipe flash, UICR and the PIB; ADR-0164: the System
    // Control Space and the FPB, through which a write could run code); otherwise
    // target.address_out_of_range (checkMemoryWrite()). RAM stays writable, so a memory write can
    // still make the device run arbitrary code (ADR-0164).
    firmware::AddressRange ram{};
    std::span<const firmware::AddressRange> peripherals;
    std::span<const firmware::AddressRange> writeDenied;
    // The RTT shell prompt (ADR-0104), recognised like the BLE prompt (ADR-0082) and reported as
    // ConsoleSession::promptSeen; empty = unknown (no detection). Separate from
    // BleProfile::shellPrompt: the RTT and NUS shells are different transports.
    std::string_view shellPrompt;
    // ---- plan 0021 (ADR-0165) ----
    // The firmware HARDWARIO supports for this target, downloaded and SHA-256 verified by
    // FirmwareCatalog::downloadDesignated (CHESTER `lte`: the LTE bundle); nullopt = none.
    std::optional<firmware::DesignatedFirmware> designatedFirmware = std::nullopt;
};

// A URL for one UI language (ADR-0167): `language` is a BCP 47 primary language subtag ("en",
// "cs").
struct LocalizedUrl {
    std::string_view language;
    std::string_view url;
};

// BLE facts of a device (ADR-0089, plan 0011 B3). Matching rule (ADR-0142, amends ADR-0083): the
// advertised name contains namePattern (case-sensitive); the service UUID is not a match.
struct BleProfile {
    std::string_view namePattern;     // substring of the advertised name; CHESTER: "CHESTER"
    std::string_view serviceUuid;     // NUS 6e400001-b5a3-f393-e0a9-e50e24dcca9e (lowercase)
    std::string_view rxUuid;          // ...0002... (written by us)
    std::string_view txUuid;          // ...0003... (notified by the device)
    std::string_view shellPrompt;     // "bt_nus:~$ " (ADR-0082)
    std::string_view consoleTargetId; // "app": routing rules, history and file names of that target
    // English source text in context "hio::device" (QT_TRANSLATE_NOOP), see passkeyHint().
    const char* passkeyHintSourceText = "";
    std::span<const ConnectionGuideStep> pairingGuide; // ADR-0077 step type; slots ble-*
};

struct DeviceProfile {
    std::string_view id;          // "chester"
    std::string_view displayName; // product name, not translated ("CHESTER")
    std::span<const TargetProfile> targets;
    std::string_view firmwareCatalogUrl; // ADR-0037: <url>/<id>/hex ("" = no catalog)
    // ADR-0077: how to power the device; English source text in context "hio::device"
    // (QT_TRANSLATE_NOOP), translated by the GUI ("" = no hint).
    std::string_view powerHintSourceText;
    // M5 (ADR-0089): devices without BLE leave it empty.
    std::optional<BleProfile> ble = std::nullopt;
    // Plan 0021 (ADR-0167): the documentation page listing the application firmware, per UI
    // language; English first (the fallback). Empty = no link. firmwareCatalogUrl stays the
    // download base (ADR-0037).
    std::span<const LocalizedUrl> firmwareDocsUrl;
};

// Size of the Product Information Block.
inline constexpr std::uint32_t pibSize = 128;

class ProfileRegistry {
public:
    // nullptr when no profile has this id.
    [[nodiscard]] static const DeviceProfile* find(std::string_view id) noexcept;
    [[nodiscard]] static std::span<const DeviceProfile* const> all() noexcept;
    // The profile a BLE advertisement belongs to, by the name pattern only (ADR-0142); nullptr for
    // none. `serviceUuids` is not used for matching (kept from ADR-0089's signature).
    [[nodiscard]] static const DeviceProfile*
    findByAdvertisement(std::string_view name, std::span<const std::string> serviceUuids) noexcept;
};

// nullptr when the profile has no target with this id.
[[nodiscard]] const TargetProfile* findTarget(const DeviceProfile& profile,
                                              std::string_view id) noexcept;

// The part of `target` with this FICR part number, nullptr when it is not one of its parts.
[[nodiscard]] const ChipPart* findPart(const TargetProfile& target, std::uint32_t part) noexcept;

// The translated display name of `target` (ADR-0027), e.g. "application".
[[nodiscard]] QString targetDisplayName(const TargetProfile& target);

// The translated title / body of a connection guide step and the translated power hint of a
// profile (ADR-0077).
[[nodiscard]] QString guideStepTitle(const ConnectionGuideStep& step);
[[nodiscard]] QString guideStepBody(const ConnectionGuideStep& step);
[[nodiscard]] QString powerHint(const DeviceProfile& profile);
// The translated BLE passkey hint of a profile ("" without BLE; ADR-0084).
[[nodiscard]] QString passkeyHint(const DeviceProfile& profile);

// The firmware docs URL of `profile` for `language` ("cs", "cs_CZ", "en-US": the primary subtag is
// compared); the first (English) entry for other languages; "" when the profile has none
// (ADR-0167).
[[nodiscard]] std::string_view firmwareDocsUrlFor(const DeviceProfile& profile,
                                                  std::string_view language) noexcept;

// The PIB address range of `target`.
[[nodiscard]] firmware::AddressRange pibRange(const TargetProfile& target) noexcept;

// ADR-0160: a memory read of [address, address + size) is allowed when it does not run past
// 0xFFFFFFFF. Error: target.address_out_of_range (detail: the requested range).
[[nodiscard]] Status checkMemoryRead(std::uint32_t address, std::size_t size);
// ADR-0160: a memory write of [address, address + size) must lie inside `target.ram` or one of
// `target.peripherals` and must not overlap any of `target.writeDenied`. Error:
// target.address_out_of_range (detail: the requested range and the allowed ranges, or the denied
// range it overlaps).
[[nodiscard]] Status checkMemoryWrite(const TargetProfile& target, std::uint32_t address,
                                      std::size_t size);
// ADR-0035, ADR-0036: a flash write (an image segment, `flash-write`, MCP write_flash) of
// [address, address + size) must not touch the PIB and must lie inside `target.flash` or
// `target.uicr`; bytes in the UICR need `allowUicr`. Errors: firmware.out_of_range (the PIB, or
// outside flash and UICR), firmware.uicr_in_image (UICR bytes without allowUicr). The one copy of
// these rules: planImage() and the CLI / MCP checks before the confirmation use it.
[[nodiscard]] Status checkFlashWrite(const TargetProfile& target, std::uint32_t address,
                                     std::size_t size, bool allowUicr);

} // namespace hio::device

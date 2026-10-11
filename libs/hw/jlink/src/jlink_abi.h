#pragma once

// Own minimal declarations of the JLinkARM library ABI (ADR-0028). Private to libs/hw/jlink (and
// the test-only fake library in tests/fake_jlink, which implements exactly this ABI).
//
// No SEGGER header is used or committed. Every declaration below was written from:
//   [UM08002] SEGGER "J-Link SDK" user manual (public function reference, constants, structs)
//   [pylink]  pylink-square 2.0.2 (Apache-2.0, read only): pylink/jlink.py (calls, argument and
//             return types), pylink/structs.py (struct layouts), pylink/enums.py (constants),
//             pylink/library.py (which functions use __stdcall on 32-bit Windows)
// The source of each item is given next to it. Struct sizes and the offsets we read are
// static_assert'ed; the fake library and the hardware-in-the-loop tests verify the rest.
//
// Calling convention: the JLINKARM_* functions are __cdecl; the JLINK_RTTERMINAL_* functions are
// __stdcall on 32-bit Windows ([pylink] library.py, Library._standard_calls_). On 64-bit Windows,
// Linux and macOS there is a single calling convention, so HIO_JLINK_STDCALL expands to nothing
// there (clang warns about an ignored __stdcall on x64, which -Werror would turn into an error).
//
// Types: SEGGER's U32 = uint32_t, U16 = uint16_t, U8 = uint8_t, I32/int = int32_t; `char` return
// values are booleans (0 / non-zero).
//
// Every new vendor function is added here deliberately (ADR-0028): declaration, symbol name in
// abiSymbols (jlink_library.cpp) and an export in tests/fake_jlink.

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && defined(_M_IX86)
#define HIO_JLINK_STDCALL __stdcall
#else
#define HIO_JLINK_STDCALL
#endif

namespace hio::jlink::abi {

// ---- constants --------------------------------------------------------------------------------

// JLINKARM_EMU_GetList HostIFs bit mask [pylink enums.JLinkHost].
inline constexpr int hostUsb = 1 << 0;
inline constexpr int hostIp = 1 << 1;

// EmuConnectInfo::connection values [UM08002 JLINKARM_EMU_CONNECT_INFO].
inline constexpr std::uint8_t connectionUsb = 1;
inline constexpr std::uint8_t connectionIp = 2;

// JLINKARM_TIF_Select interfaces [pylink enums.JLinkInterfaces].
inline constexpr int tifJtag = 0;
inline constexpr int tifSwd = 1;

// JLINK_RTTERMINAL_Control commands [pylink enums.JLinkRTTCommand].
inline constexpr std::uint32_t rttCmdStart = 0;
inline constexpr std::uint32_t rttCmdStop = 1;
inline constexpr std::uint32_t rttCmdGetDesc = 2;
inline constexpr std::uint32_t rttCmdGetNumBuf = 3;

// RTT buffer directions [pylink enums.JLinkRTTDirection].
inline constexpr std::uint32_t rttDirectionUp = 0;
inline constexpr std::uint32_t rttDirectionDown = 1;

// Buffer sizes we pass for text outputs (pylink 2.0.2 uses 336 for MAX_BUF_SIZE, jlink.py).
inline constexpr std::size_t textBufferSize = 512;

// ---- structs ----------------------------------------------------------------------------------

// JLINKARM_EMU_CONNECT_INFO [UM08002; pylink structs.JLinkConnectInfo], natural alignment.
struct EmuConnectInfo {
    std::uint32_t serialNumber;
    std::uint8_t connection; // connectionUsb / connectionIp
    std::uint32_t usbAddr;
    std::array<std::uint8_t, 16> ipAddr;
    std::int32_t time;
    std::uint64_t timeUs;
    std::uint32_t hwVersion;
    std::array<std::uint8_t, 6> macAddr;
    std::array<char, 32> product;
    std::array<char, 32> nickname;
    std::array<char, 112> firmwareString;
    char isDhcpAssignedIp;
    char isDhcpAssignedIpIsValid;
    char numIpConnections;
    char numIpConnectionsIsValid;
    std::array<std::uint8_t, 34> padding;
};
static_assert(sizeof(EmuConnectInfo) == 264, "JLINKARM_EMU_CONNECT_INFO is 264 bytes");
static_assert(offsetof(EmuConnectInfo, serialNumber) == 0);
static_assert(offsetof(EmuConnectInfo, connection) == 4);
static_assert(offsetof(EmuConnectInfo, timeUs) == 32);
static_assert(offsetof(EmuConnectInfo, product) == 50);
static_assert(offsetof(EmuConnectInfo, firmwareString) == 114);

// JLINKARM_HW_STATUS [UM08002; pylink structs.JLinkHardwareStatus].
struct HwStatus {
    std::uint16_t vTarget; // target voltage in mV
    std::uint8_t tck;
    std::uint8_t tdi;
    std::uint8_t tdo;
    std::uint8_t tms;
    std::uint8_t tres;
    std::uint8_t trst;
};
static_assert(sizeof(HwStatus) == 8, "JLINKARM_HW_STATUS is 8 bytes");

// JLINK_RTTERMINAL_START [pylink structs.JLinkRTTerminalStart].
struct RttStart {
    std::uint32_t configBlockAddress;
    std::array<std::uint32_t, 3> reserved;
};
static_assert(sizeof(RttStart) == 16, "JLINK_RTTERMINAL_START is 16 bytes");

// JLINK_RTTERMINAL_BUFDESC [pylink structs.JLinkRTTerminalBufDesc].
struct RttBufDesc {
    std::int32_t bufferIndex;
    std::uint32_t direction; // rttDirectionUp / rttDirectionDown
    std::array<char, 32> name;
    std::uint32_t sizeOfBuffer;
    std::uint32_t flags;
};
static_assert(sizeof(RttBufDesc) == 48, "JLINK_RTTERMINAL_BUFDESC is 48 bytes");
static_assert(offsetof(RttBufDesc, name) == 8);
static_assert(offsetof(RttBufDesc, sizeOfBuffer) == 40);

// ---- functions --------------------------------------------------------------------------------
// [UM08002] signatures, cross-checked with the calls in [pylink] jlink.py.

// JLINKARM_LOG: void (*)(const char* sText) - log / warning / error out handler.
using LogHandler = void (*)(const char*);

using GetDllVersionFn = std::uint32_t (*)();             // JLINKARM_GetDLLVersion
using GetCompileDateTimeFn = const char* (*)();          // JLINKARM_GetCompileDateTime
using EmuGetListFn = int (*)(int, EmuConnectInfo*, int); // JLINKARM_EMU_GetList
using EmuSelectByUsbSnFn = int (*)(std::uint32_t);       // JLINKARM_EMU_SelectByUSBSN
using OpenFn = const char* (*)();                        // JLINKARM_Open
using CloseFn = void (*)();                              // JLINKARM_Close
using IsOpenFn = char (*)();                             // JLINKARM_IsOpen
using ExecCommandFn = int (*)(const char*, char*, int);  // JLINKARM_ExecCommand
using TifSelectFn = int (*)(int);                        // JLINKARM_TIF_Select
using SetSpeedFn = void (*)(std::uint32_t);              // JLINKARM_SetSpeed
using ConnectFn = int (*)();                             // JLINKARM_Connect
using IsConnectedFn = char (*)();                        // JLINKARM_IsConnected
using GetHwStatusFn = int (*)(HwStatus*);                // JLINKARM_GetHWStatus
using GetFirmwareStringFn = void (*)(char*, int);        // JLINKARM_GetFirmwareString
using GetEmbeddedFwStringFn = int (*)(const char*, char*, std::uint32_t); // ..._GetEmbeddedFWString
using UpdateFirmwareIfNewerFn = std::uint32_t (*)(); // JLINKARM_UpdateFirmwareIfNewer
using GetSnFn = int (*)();                           // JLINKARM_GetSN
using SetResetDelayFn = void (*)(int);               // JLINKARM_SetResetDelay
using ResetFn = int (*)();                           // JLINKARM_Reset
using HaltFn = char (*)();                           // JLINKARM_Halt (0 = halted)
using GoFn = void (*)();                             // JLINKARM_Go
using SetOutHandlerFn = void (*)(LogHandler);        // JLINKARM_Set{Log,Warn,Error}OutHandler
using RttControlFn = int(HIO_JLINK_STDCALL*)(std::uint32_t, void*); // JLINK_RTTERMINAL_Control
using RttReadFn = int(HIO_JLINK_STDCALL*)(std::uint32_t, char*, std::uint32_t);        // ..._Read
using RttWriteFn = int(HIO_JLINK_STDCALL*)(std::uint32_t, const char*, std::uint32_t); // ..._Write

// The resolved function table of one loaded library copy.
struct Functions {
    GetDllVersionFn getDllVersion = nullptr;
    GetCompileDateTimeFn getCompileDateTime = nullptr;
    EmuGetListFn emuGetList = nullptr;
    EmuSelectByUsbSnFn emuSelectByUsbSn = nullptr;
    OpenFn open = nullptr;
    CloseFn close = nullptr;
    IsOpenFn isOpen = nullptr;
    ExecCommandFn execCommand = nullptr;
    TifSelectFn tifSelect = nullptr;
    SetSpeedFn setSpeed = nullptr;
    ConnectFn connect = nullptr;
    IsConnectedFn isConnected = nullptr;
    GetHwStatusFn getHwStatus = nullptr;
    GetFirmwareStringFn getFirmwareString = nullptr;
    GetEmbeddedFwStringFn getEmbeddedFwString = nullptr;
    UpdateFirmwareIfNewerFn updateFirmwareIfNewer = nullptr;
    GetSnFn getSn = nullptr;
    SetResetDelayFn setResetDelay = nullptr;
    ResetFn reset = nullptr;
    HaltFn halt = nullptr;
    GoFn go = nullptr;
    SetOutHandlerFn setLogOutHandler = nullptr;
    SetOutHandlerFn setWarnOutHandler = nullptr;
    SetOutHandlerFn setErrorOutHandler = nullptr;
    RttControlFn rttControl = nullptr;
    RttReadFn rttRead = nullptr;
    RttWriteFn rttWrite = nullptr;
};

} // namespace hio::jlink::abi

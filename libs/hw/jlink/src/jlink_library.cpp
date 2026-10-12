#include "hio/hw/jlink/jlink_library.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "jlink_abi.h"
#include "out_handlers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFunctionPointer>
#include <QLibrary>
#include <QLockFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QtCore/qnamespace.h>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::jlink {

namespace {

namespace fs = std::filesystem;

// Bounds (plan/features/0002-m1-jlink-rtt.md): at most this many probes are listed, and at most
// this many library error texts are kept for the next hio::Error's detail.
constexpr int maxListedProbes = 64;
constexpr std::size_t maxRecentErrors = 4;

// Lock file inside each instance directory (review R14).
constexpr const char* instanceLockName = "instance.lock";

// A NUL-terminated (or full) char array from the library as UTF-8; invalid sequences replaced.
std::string fromCharArray(std::span<const char> chars) {
    std::size_t length = 0;
    while (length < chars.size() && chars[length] != '\0') {
        ++length;
    }
    return QString::fromUtf8(chars.data(), static_cast<qsizetype>(length)).toStdString();
}

std::string fromCString(const char* text) {
    return text == nullptr ? std::string() : QString::fromUtf8(text).toStdString();
}

template <class Fn>
void resolveInto(QLibrary& library, const char* name, Fn& target,
                 std::vector<std::string>& missing) {
    const QFunctionPointer symbol = library.resolve(name);
    if (symbol == nullptr) {
        missing.emplace_back(name);
        return;
    }
    // Vendor adapter (ADR-0028): the symbol has the type declared in jlink_abi.h.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    target = reinterpret_cast<Fn>(symbol);
}

std::vector<std::string> resolveAll(QLibrary& library, abi::Functions& fn) {
    std::vector<std::string> missing;
    resolveInto(library, "JLINKARM_GetDLLVersion", fn.getDllVersion, missing);
    resolveInto(library, "JLINKARM_GetCompileDateTime", fn.getCompileDateTime, missing);
    resolveInto(library, "JLINKARM_EMU_GetList", fn.emuGetList, missing);
    resolveInto(library, "JLINKARM_EMU_SelectByUSBSN", fn.emuSelectByUsbSn, missing);
    resolveInto(library, "JLINKARM_Open", fn.open, missing);
    resolveInto(library, "JLINKARM_Close", fn.close, missing);
    resolveInto(library, "JLINKARM_IsOpen", fn.isOpen, missing);
    resolveInto(library, "JLINKARM_ExecCommand", fn.execCommand, missing);
    resolveInto(library, "JLINKARM_TIF_Select", fn.tifSelect, missing);
    resolveInto(library, "JLINKARM_SetSpeed", fn.setSpeed, missing);
    resolveInto(library, "JLINKARM_Connect", fn.connect, missing);
    resolveInto(library, "JLINKARM_IsConnected", fn.isConnected, missing);
    resolveInto(library, "JLINKARM_GetHWStatus", fn.getHwStatus, missing);
    resolveInto(library, "JLINKARM_GetFirmwareString", fn.getFirmwareString, missing);
    resolveInto(library, "JLINKARM_GetEmbeddedFWString", fn.getEmbeddedFwString, missing);
    resolveInto(library, "JLINKARM_UpdateFirmwareIfNewer", fn.updateFirmwareIfNewer, missing);
    resolveInto(library, "JLINKARM_GetSN", fn.getSn, missing);
    resolveInto(library, "JLINKARM_SetResetDelay", fn.setResetDelay, missing);
    resolveInto(library, "JLINKARM_Reset", fn.reset, missing);
    resolveInto(library, "JLINKARM_Halt", fn.halt, missing);
    resolveInto(library, "JLINKARM_Go", fn.go, missing);
    resolveInto(library, "JLINKARM_SetLogOutHandler", fn.setLogOutHandler, missing);
    resolveInto(library, "JLINKARM_SetWarnOutHandler", fn.setWarnOutHandler, missing);
    resolveInto(library, "JLINKARM_SetErrorOutHandler", fn.setErrorOutHandler, missing);
    resolveInto(library, "JLINK_RTTERMINAL_Control", fn.rttControl, missing);
    resolveInto(library, "JLINK_RTTERMINAL_Read", fn.rttRead, missing);
    resolveInto(library, "JLINK_RTTERMINAL_Write", fn.rttWrite, missing);
    resolveInto(library, "JLINKARM_BeginDownload", fn.beginDownload, missing);
    resolveInto(library, "JLINKARM_WriteMem", fn.writeMem, missing);
    resolveInto(library, "JLINKARM_EndDownload", fn.endDownload, missing);
    resolveInto(library, "JLINKARM_ReadMemEx", fn.readMemEx, missing);
    resolveInto(library, "JLINKARM_ReadMemU32", fn.readMemU32, missing);
    resolveInto(library, "JLINK_EraseChip", fn.eraseChip, missing);
    resolveInto(library, "JLINK_SetFlashProgProgressCallback", fn.setFlashProgress, missing);
    resolveInto(library, "JLINK_SetHookUnsecureDialog", fn.setHookUnsecureDialog, missing);
    resolveInto(library, "JLINKARM_CORESIGHT_Configure", fn.coresightConfigure, missing);
    resolveInto(library, "JLINKARM_CORESIGHT_ReadAPDPReg", fn.coresightRead, missing);
    resolveInto(library, "JLINKARM_CORESIGHT_WriteAPDPReg", fn.coresightWrite, missing);
    return missing;
}

std::string joined(const std::vector<std::string>& items) {
    std::string text;
    for (const std::string& item : items) {
        text += text.empty() ? item : ", " + item;
    }
    return text;
}

// Loading and unloading of library copies is serialized in the process: copies are loaded and
// unloaded by different threads (sessions, the GUI probe scanner, ADR-0093), and a new copy may be
// mapped where an unloaded one was. ld.so's own locking is invisible to ThreadSanitizer, so without
// this lock TSan reports the new copy's first accesses as races with the old copy's last ones.
std::mutex& libraryLoadMutex() {
    static std::mutex mutex;
    return mutex;
}

// Slots of library copies that could not be unloaded (see JLinkLibrary::Impl::~Impl). At most
// OutHandlerSlot::maxSlots; destroyed at process exit.
void keepForProcessLifetime(std::unique_ptr<OutHandlerSlot> slot) {
    static std::mutex mutex;
    static std::vector<std::unique_ptr<OutHandlerSlot>> kept;
    const std::scoped_lock lock(mutex);
    kept.push_back(std::move(slot));
}

// The flash loader's progress callback has no user data: it is routed to the FlashProgressFn of
// the endDownload() call running on this thread (the library calls it synchronously from inside
// EndDownload, to confirm in Phase 0; a call from another thread finds no sink and is ignored).
// C callback: never throws, never touches Qt objects.
// Non-const by design: per-thread routing of the C callback, set only by FlashProgressScope.
thread_local const FlashProgressFn*
    currentFlashProgress = // NOLINT(*-avoid-non-const-global-variables)
    nullptr;

void flashProgressCallback(const char* action, const char* /*progress*/, int percent) noexcept {
    const FlashProgressFn* sink = currentFlashProgress;
    if (sink == nullptr || !*sink) {
        return;
    }
    try {
        (*sink)(action == nullptr ? std::string_view() : std::string_view(action), percent);
    } catch (...) { // NOLINT(bugprone-empty-catch): nothing may cross the C boundary
    }
}

// Review R1: the library asks "unsecure (mass-erase) the device?" when it meets a read-back
// protected chip. Batch mode would take the dialog's default answer and may erase the chip incl.
// the PIB without the ADR-0038 confirmation, so this hook always answers NO (as pylink's default
// util.unsecure_hook_dialog). Erasing a protected chip is only done by `recover`. C callback:
// never throws, never touches Qt objects.
int unsecureDialogHook(const char* /*title*/, const char* /*message*/,
                       std::uint32_t /*flags*/) noexcept {
    return abi::dlgButtonNo;
}

// Sets currentFlashProgress for the lifetime of one vendor call.
class FlashProgressScope {
public:
    explicit FlashProgressScope(const FlashProgressFn& progress) {
        currentFlashProgress = &progress;
    }
    ~FlashProgressScope() { currentFlashProgress = nullptr; }
    FlashProgressScope(const FlashProgressScope&) = delete;
    FlashProgressScope& operator=(const FlashProgressScope&) = delete;
    FlashProgressScope(FlashProgressScope&&) = delete;
    FlashProgressScope& operator=(FlashProgressScope&&) = delete;
};

Error loadFailed(std::string detail) {
    return makeError(ErrorCode::DependencyJLinkLoadFailed, std::move(detail));
}

} // namespace

struct JLinkLibrary::Impl {
    fs::path sourcePath;
    fs::path copyPath;
    std::unique_ptr<QTemporaryDir> directory; // removed (with the copy) in the destructor
    // Held for the whole lifetime in the instance directory, so cleanupStaleInstances() of
    // another process never removes a copy in use (review R14).
    std::unique_ptr<QLockFile> instanceLock;
    std::unique_ptr<QLibrary> library;
    std::unique_ptr<OutHandlerSlot> slot;
    abi::Functions fn;
    std::deque<std::string> recentErrors; // bounded by maxRecentErrors
    std::uint64_t reportedDrops = 0;      // dropped messages already logged
    std::uint64_t reportedLost = 0;       // lost messages (all slots) already logged

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() {
        if (library && library->isLoaded()) {
            if (fn.isOpen != nullptr && fn.close != nullptr && fn.isOpen() != 0) {
                fn.close();
            }
            for (abi::SetOutHandlerFn set :
                 {fn.setLogOutHandler, fn.setWarnOutHandler, fn.setErrorOutHandler}) {
                if (set != nullptr) {
                    set(nullptr);
                }
            }
            if (fn.setFlashProgress != nullptr) {
                fn.setFlashProgress(nullptr);
            }
            bool unloaded = false;
            {
                const std::scoped_lock lock(libraryLoadMutex());
                unloaded = library->unload();
            }
            if (!unloaded) {
                // The copy stays loaded and may still call its handlers: keep the slot (and its
                // queue) for the rest of the process instead of freeing memory the library can
                // reach (review fix 6). Bounded by OutHandlerSlot::maxSlots.
                qCWarning(log::lcJLink)
                    << "Unloading the J-Link library copy failed:" << library->errorString();
                keepForProcessLifetime(std::move(slot));
            }
        }
        library.reset();
        slot.reset();           // after unloading: no handler of this copy can run any more
        instanceLock = nullptr; // unlocks before the directory is removed
        directory.reset();
    }
};

Result<std::unique_ptr<JLinkLibrary>> JLinkLibrary::loadCopy(const LibraryLocation& location,
                                                             const fs::path& instanceRoot) {
    std::error_code ec;
    if (!fs::is_regular_file(location.path, ec)) {
        return std::unexpected(
            makeError(ErrorCode::DependencyJLinkNotFound, "not found: " + toUtf8(location.path)));
    }
    auto impl = std::make_unique<Impl>();
    impl->sourcePath = location.path;

    fs::create_directories(instanceRoot, ec);
    impl->directory =
        std::make_unique<QTemporaryDir>(toQString(toUtf8(instanceRoot / "instance-XXXXXX")));
    if (!impl->directory->isValid()) {
        return std::unexpected(loadFailed("cannot create an instance directory under " +
                                          toUtf8(instanceRoot) + ": " +
                                          impl->directory->errorString().toStdString()));
    }
    impl->instanceLock = std::make_unique<QLockFile>(
        toQString(toUtf8(fs::path(impl->directory->path().toStdU16String()) / instanceLockName)));
    impl->instanceLock->setStaleLockTime(std::chrono::milliseconds(0));
    if (!impl->instanceLock->tryLock(std::chrono::milliseconds(0))) {
        return std::unexpected(loadFailed("cannot lock the instance directory " +
                                          impl->directory->path().toStdString()));
    }
    impl->copyPath = fs::path(impl->directory->path().toStdU16String()) / location.path.filename();
    fs::copy_file(location.path, impl->copyPath, ec);
    if (ec) {
        return std::unexpected(loadFailed("cannot copy " + toUtf8(location.path) + " to " +
                                          toUtf8(impl->copyPath) + ": " + ec.message()));
    }

    impl->library = std::make_unique<QLibrary>(toQString(toUtf8(impl->copyPath)));
    bool loaded = false;
    {
        const std::scoped_lock lock(libraryLoadMutex());
        loaded = impl->library->load();
    }
    if (!loaded) {
        return std::unexpected(
            loadFailed(toUtf8(location.path) + ": " + impl->library->errorString().toStdString()));
    }
    const std::vector<std::string> missing = resolveAll(*impl->library, impl->fn);
    if (!missing.empty()) {
        return std::unexpected(
            loadFailed(toUtf8(location.path) + ": missing functions: " + joined(missing)));
    }

    const std::uint32_t version = impl->fn.getDllVersion();
    if (version < minimumLibraryVersion) {
        Error error = makeError(ErrorCode::DependencyJLinkTooOld,
                                std::format("{} reports version {} ({}); minimum is {}",
                                            toUtf8(location.path), formatLibraryVersion(version),
                                            version, formatLibraryVersion(minimumLibraryVersion)));
        error.message = QCoreApplication::translate("hio::jlink", "J-Link Software %1 is too old.")
                            .arg(toQString(formatLibraryVersion(version)))
                            .toStdString();
        return std::unexpected(std::move(error));
    }

    auto slot = OutHandlerSlot::acquire();
    if (!slot) {
        return std::unexpected(std::move(slot).error());
    }
    impl->slot = std::move(*slot);
    impl->fn.setLogOutHandler(impl->slot->logHandler());
    impl->fn.setWarnOutHandler(impl->slot->warningHandler());
    impl->fn.setErrorOutHandler(impl->slot->errorHandler());
    impl->fn.setFlashProgress(&flashProgressCallback);

    qCInfo(log::lcJLink).noquote()
        << QStringLiteral("J-Link library %1 (%2) loaded from %3 as %4")
               .arg(toQString(formatLibraryVersion(version)),
                    toQString(fromCString(impl->fn.getCompileDateTime())),
                    toQString(toUtf8(location.path)), toQString(toUtf8(impl->copyPath)));
    return std::make_unique<JLinkLibrary>(Passkey{}, std::move(impl));
}

fs::path JLinkLibrary::defaultInstanceRoot() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "jlink-instances";
}

std::size_t JLinkLibrary::cleanupStaleInstances(const fs::path& root, std::chrono::hours maxAge) {
    std::size_t removed = 0;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (!it->is_directory(entryError)) {
            continue;
        }
        const auto modified = it->last_write_time(entryError);
        if (entryError || now - modified < maxAge) {
            continue;
        }
        // Old enough; removed only when its owner is gone (the lock can be taken: stale, owner
        // process dead) or it has no lock (review R14).
        const fs::path lockPath = it->path() / instanceLockName;
        if (fs::exists(lockPath, entryError)) {
            QLockFile lock(toQString(toUtf8(lockPath)));
            lock.setStaleLockTime(std::chrono::milliseconds(0));
            if (!lock.tryLock(std::chrono::milliseconds(0))) {
                continue; // a live process still uses this copy
            }
            lock.unlock();
        }
        if (fs::remove_all(it->path(), entryError) > 0 && !entryError) {
            ++removed;
            qCInfo(log::lcJLink) << "Removed stale J-Link instance directory"
                                 << toQString(toUtf8(it->path()));
        }
    }
    return removed;
}

JLinkLibrary::JLinkLibrary(Passkey /*key*/, std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

JLinkLibrary::~JLinkLibrary() = default;

const fs::path& JLinkLibrary::sourcePath() const noexcept { return m_impl->sourcePath; }

const fs::path& JLinkLibrary::copyPath() const noexcept { return m_impl->copyPath; }

std::uint32_t JLinkLibrary::libraryVersion() { return m_impl->fn.getDllVersion(); }

std::string JLinkLibrary::compileDateTime() { return fromCString(m_impl->fn.getCompileDateTime()); }

std::expected<std::vector<ProbeInfo>, int> JLinkLibrary::listProbes(ProbeHosts hosts) {
    const int hostMask = hosts == ProbeHosts::Usb ? abi::hostUsb : abi::hostUsb | abi::hostIp;
    const int count = m_impl->fn.emuGetList(hostMask, nullptr, 0);
    if (count < 0) {
        return std::unexpected(count);
    }
    std::vector<abi::EmuConnectInfo> infos(
        static_cast<std::size_t>(std::min(count, maxListedProbes)), abi::EmuConnectInfo{});
    int found = 0;
    if (!infos.empty()) {
        found = m_impl->fn.emuGetList(hostMask, infos.data(), static_cast<int>(infos.size()));
        if (found < 0) {
            return std::unexpected(found);
        }
    }
    std::vector<ProbeInfo> probes;
    for (std::size_t i = 0; i < std::min(infos.size(), static_cast<std::size_t>(found)); ++i) {
        const abi::EmuConnectInfo& info = infos[i];
        probes.push_back(ProbeInfo{
            .serialNumber = info.serialNumber,
            .product = fromCharArray(info.product),
            .connection = info.connection == abi::connectionIp ? Connection::Ip : Connection::Usb,
            .firmware = fromCharArray(info.firmwareString)});
    }
    return probes;
}

int JLinkLibrary::selectBySerialNumber(std::uint32_t serialNumber) {
    return m_impl->fn.emuSelectByUsbSn(serialNumber);
}

std::optional<std::string> JLinkLibrary::open() {
    const char* error = m_impl->fn.open();
    if (error == nullptr) {
        return std::nullopt;
    }
    std::string text = fromCString(error);
    return text.empty() ? std::string("unknown error") : text;
}

void JLinkLibrary::close() { m_impl->fn.close(); }

bool JLinkLibrary::isOpen() { return m_impl->fn.isOpen() != 0; }

std::string JLinkLibrary::execCommand(std::string_view command) {
    std::array<char, abi::textBufferSize> output{};
    const std::string commandText(command);
    (void)m_impl->fn.execCommand(commandText.c_str(), output.data(),
                                 static_cast<int>(output.size()));
    return fromCharArray(output);
}

int JLinkLibrary::selectInterface(TargetInterface targetInterface) {
    return m_impl->fn.tifSelect(targetInterface == TargetInterface::Swd ? abi::tifSwd
                                                                        : abi::tifJtag);
}

void JLinkLibrary::setSpeed(std::uint32_t kiloHertz) { m_impl->fn.setSpeed(kiloHertz); }

int JLinkLibrary::connect() { return m_impl->fn.connect(); }

bool JLinkLibrary::isConnected() { return m_impl->fn.isConnected() != 0; }

std::optional<HardwareStatus> JLinkLibrary::hardwareStatus() {
    abi::HwStatus status{};
    if (m_impl->fn.getHwStatus(&status) != 0) {
        return std::nullopt;
    }
    return HardwareStatus{.targetMillivolts = status.vTarget};
}

std::string JLinkLibrary::firmwareString() {
    std::array<char, abi::textBufferSize> buffer{};
    m_impl->fn.getFirmwareString(buffer.data(), static_cast<int>(buffer.size()));
    return fromCharArray(buffer);
}

std::expected<std::string, int> JLinkLibrary::embeddedFirmwareString(std::string_view id) {
    std::array<char, abi::textBufferSize> buffer{};
    const std::string idText(id);
    const int result = m_impl->fn.getEmbeddedFwString(idText.c_str(), buffer.data(),
                                                      static_cast<std::uint32_t>(buffer.size()));
    if (result < 0) {
        return std::unexpected(result);
    }
    return fromCharArray(buffer);
}

std::uint32_t JLinkLibrary::updateFirmwareIfNewer() { return m_impl->fn.updateFirmwareIfNewer(); }

std::uint32_t JLinkLibrary::serialNumber() {
    return static_cast<std::uint32_t>(m_impl->fn.getSn());
}

void JLinkLibrary::setResetDelay(int milliseconds) { m_impl->fn.setResetDelay(milliseconds); }

int JLinkLibrary::reset() { return m_impl->fn.reset(); }

int JLinkLibrary::halt() { return static_cast<int>(m_impl->fn.halt()); }

void JLinkLibrary::go() { m_impl->fn.go(); }

int JLinkLibrary::rttStart() { return m_impl->fn.rttControl(abi::rttCmdStart, nullptr); }

int JLinkLibrary::rttStop() { return m_impl->fn.rttControl(abi::rttCmdStop, nullptr); }

int JLinkLibrary::rttBufferCount(RttDirection direction) {
    std::uint32_t config =
        direction == RttDirection::Up ? abi::rttDirectionUp : abi::rttDirectionDown;
    return m_impl->fn.rttControl(abi::rttCmdGetNumBuf, &config);
}

std::expected<RttBufferInfo, int> JLinkLibrary::rttBufferInfo(int index, RttDirection direction) {
    abi::RttBufDesc desc{};
    desc.bufferIndex = index;
    desc.direction = direction == RttDirection::Up ? abi::rttDirectionUp : abi::rttDirectionDown;
    const int result = m_impl->fn.rttControl(abi::rttCmdGetDesc, &desc);
    if (result < 0) {
        return std::unexpected(result);
    }
    return RttBufferInfo{
        .name = fromCharArray(desc.name), .size = desc.sizeOfBuffer, .flags = desc.flags};
}

int JLinkLibrary::rttRead(int index, std::span<std::byte> buffer) {
    if (index < 0 || buffer.size() > std::numeric_limits<std::uint32_t>::max()) {
        return -1;
    }
    // Vendor adapter (ADR-0028): the library takes the buffer as char*.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    char* data = reinterpret_cast<char*>(buffer.data());
    return m_impl->fn.rttRead(static_cast<std::uint32_t>(index), data,
                              static_cast<std::uint32_t>(buffer.size()));
}

int JLinkLibrary::rttWrite(int index, std::span<const std::byte> data) {
    if (index < 0 || data.size() > std::numeric_limits<std::uint32_t>::max()) {
        return -1;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): vendor adapter, see rttRead
    const char* bytes = reinterpret_cast<const char*>(data.data());
    return m_impl->fn.rttWrite(static_cast<std::uint32_t>(index), bytes,
                               static_cast<std::uint32_t>(data.size()));
}

void JLinkLibrary::beginDownload() { m_impl->fn.beginDownload(0); }

int JLinkLibrary::writeMemory(std::uint32_t address, std::span<const std::byte> data) {
    if (data.size() > std::numeric_limits<std::uint32_t>::max()) {
        return -1;
    }
    return m_impl->fn.writeMem(address, static_cast<std::uint32_t>(data.size()), data.data());
}

int JLinkLibrary::endDownload(const FlashProgressFn& progress) {
    const FlashProgressScope scope(progress);
    return m_impl->fn.endDownload();
}

int JLinkLibrary::readMemory(std::uint32_t address, std::span<std::byte> buffer) {
    if (buffer.size() > std::numeric_limits<std::uint32_t>::max()) {
        return -1;
    }
    return m_impl->fn.readMemEx(address, static_cast<std::uint32_t>(buffer.size()), buffer.data(),
                                0);
}

std::expected<std::uint32_t, int> JLinkLibrary::readU32(std::uint32_t address) {
    std::uint32_t value = 0;
    std::uint8_t status = 0;
    const int result = m_impl->fn.readMemU32(address, 1, &value, &status);
    if (result < 1) {
        return std::unexpected(result < 0 ? result : -1);
    }
    if (status != 0) {
        return std::unexpected(-static_cast<int>(status));
    }
    return value;
}

int JLinkLibrary::eraseChip() { return m_impl->fn.eraseChip(); }

int JLinkLibrary::installUnsecureDialogHook() {
    return m_impl->fn.setHookUnsecureDialog(&unsecureDialogHook);
}

int JLinkLibrary::unsecureDialogAnswer() { return unsecureDialogHook("test", "test", 0); }

int JLinkLibrary::coresightConfigure() { return m_impl->fn.coresightConfigure(""); }

std::expected<std::uint32_t, int> JLinkLibrary::coresightRead(std::uint8_t index, bool ap) {
    std::uint32_t value = 0;
    const int result =
        m_impl->fn.coresightRead(index, ap ? abi::coresightAp : abi::coresightDp, &value);
    if (result < 0) {
        return std::unexpected(result);
    }
    return value;
}

int JLinkLibrary::coresightWrite(std::uint8_t index, bool ap, std::uint32_t value) {
    return m_impl->fn.coresightWrite(index, ap ? abi::coresightAp : abi::coresightDp, value);
}

void JLinkLibrary::flushMessages() {
    while (auto message = m_impl->slot->queue().pop({}, std::chrono::milliseconds(0))) {
        const QString text = toQString(message->text).trimmed();
        // The probe firmware must never change by itself (plan/features/0002): make any
        // firmware-update message visible, whatever its kind (decided 2026-10-10).
        if (text.contains(QStringLiteral("firmware"), Qt::CaseInsensitive) &&
            text.contains(QStringLiteral("updat"), Qt::CaseInsensitive)) {
            qCWarning(log::lcJLink).noquote() << "J-Link firmware update message:" << text;
        }
        switch (message->kind) {
        case LibraryMessage::Kind::Log:
            qCDebug(log::lcJLink).noquote() << "J-Link:" << text;
            break;
        case LibraryMessage::Kind::Warning:
            qCInfo(log::lcJLink).noquote() << "J-Link warning:" << text;
            break;
        case LibraryMessage::Kind::Error:
            qCWarning(log::lcJLink).noquote() << "J-Link error:" << text;
            m_impl->recentErrors.push_back(text.toStdString());
            while (m_impl->recentErrors.size() > maxRecentErrors) {
                m_impl->recentErrors.pop_front();
            }
            break;
        }
    }
    const std::uint64_t dropped = m_impl->slot->queue().droppedCount();
    if (dropped != m_impl->reportedDrops) {
        qCDebug(log::lcJLink) << "J-Link library messages dropped (queue full):"
                              << dropped - m_impl->reportedDrops;
        m_impl->reportedDrops = dropped;
    }
    const std::uint64_t lost = OutHandlerSlot::lostMessages();
    if (lost != m_impl->reportedLost) {
        qCWarning(log::lcJLink) << "J-Link library messages lost (out of memory):"
                                << lost - m_impl->reportedLost;
        m_impl->reportedLost = lost;
    }
}

std::string JLinkLibrary::takeErrorText() {
    flushMessages();
    std::string text;
    for (const std::string& error : m_impl->recentErrors) {
        text += text.empty() ? error : "; " + error;
    }
    m_impl->recentErrors.clear();
    return text;
}

} // namespace hio::jlink

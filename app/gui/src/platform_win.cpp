// Windows identity (plan/features/0015 A3): the AppUserModelID of the process, the same as the
// Start-menu shortcut's System.AppUserModel.ID (packaging/windows/wix).

// NOMINMAX and WIN32_LEAN_AND_MEAN are set by app/gui/CMakeLists.txt.
#include <windows.h>
// <windows.h> must come first: <shobjidl_core.h> needs its types. This comment ends the include
// block, so clang-format (IncludeBlocks: Regroup) does not sort it below.
#include "gui_log.h"
#include "platform.h"

#include <QLoggingCategory>
#include <QtLogging>

#include <shobjidl_core.h>

namespace hio::gui {

void applyPlatformIdentity() {
    const HRESULT result = SetCurrentProcessExplicitAppUserModelID(L"com.hardwario.monitor");
    if (FAILED(result)) {
        qCWarning(lcGui) << "SetCurrentProcessExplicitAppUserModelID failed:"
                         << static_cast<long>(result);
    }
}

} // namespace hio::gui

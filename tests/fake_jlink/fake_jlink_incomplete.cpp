// A test-only library with a single J-Link export: JLinkLibrary::loadCopy must reject it with
// dependency.jlink_load_failed naming the missing functions (plan/features/0002, phase A1).

#include <cstdint>

#ifdef _WIN32
#define HIO_FAKE_JLINK_EXPORT __declspec(dllexport)
#else
#define HIO_FAKE_JLINK_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HIO_FAKE_JLINK_EXPORT std::uint32_t JLINKARM_GetDLLVersion() { return 81000; }

# Test-only fake of Nordic's nRF Util (plan/features/0003-m2-flashing.md, D2): an executable named
# `nrfutil` in its own directory (so PATH lookups find it). Never installed or shipped.

add_executable(hio_fake_nrfutil fake_nrfutil.cpp)
hio_apply_target_defaults(hio_fake_nrfutil)
set_target_properties(hio_fake_nrfutil PROPERTIES
    OUTPUT_NAME nrfutil
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
# The MSVC CRT deprecates std::getenv (unsafe across threads); this tool is single-threaded and
# only reads its HIO_FAKE_NRFUTIL_* settings, so the warning is disabled for this target only.
if(WIN32)
    target_compile_definitions(hio_fake_nrfutil PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

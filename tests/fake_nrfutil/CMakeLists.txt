# Test-only fake of Nordic's nRF Util (plan/features/0003-m2-flashing.md, D2): an executable named
# `nrfutil` in its own directory (so PATH lookups find it). Never installed or shipped.

add_executable(hwio_fake_nrfutil fake_nrfutil.cpp)
hwio_apply_target_defaults(hwio_fake_nrfutil)
set_target_properties(hwio_fake_nrfutil PROPERTIES
    OUTPUT_NAME nrfutil
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

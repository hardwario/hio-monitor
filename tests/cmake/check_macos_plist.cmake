# Checks the macOS Info.plist of the CLI (embedded __TEXT,__info_plist section) or of the GUI
# bundle (ADR-0086, plan/features/0011 D1). Run in script mode:
#   cmake -DHIO_BINARY=<hio executable | bundle Info.plist> -DHIO_KIND=cli|gui -P <this file>
set(_key "NSBluetoothAlwaysUsageDescription")
if(HIO_KIND STREQUAL "cli")
    execute_process(COMMAND otool -l "${HIO_BINARY}" OUTPUT_VARIABLE _load RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0 OR NOT _load MATCHES "sectname __info_plist")
        message(FATAL_ERROR "${HIO_BINARY} has no __TEXT,__info_plist section")
    endif()
    # `launchctl plist` prints the embedded plist of a Mach-O binary.
    execute_process(COMMAND launchctl plist "__TEXT,__info_plist" "${HIO_BINARY}"
        OUTPUT_VARIABLE _plist RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        execute_process(COMMAND strings -a "${HIO_BINARY}" OUTPUT_VARIABLE _plist)
    endif()
    set(_identifier "com.hardwario.monitor.cli")
else()
    file(READ "${HIO_BINARY}" _plist)
    set(_identifier "com.hardwario.monitor")
endif()
foreach(_expected IN ITEMS "${_key}" "${_identifier}")
    string(FIND "${_plist}" "${_expected}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "${HIO_BINARY}: '${_expected}' missing from the Info.plist")
    endif()
endforeach()
message(STATUS "${HIO_BINARY}: Info.plist has ${_key}")

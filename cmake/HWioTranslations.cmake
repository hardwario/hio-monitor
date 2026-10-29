# Translations (ADR-0027, plan/features/0001-m0-skeleton.md).
#
# One project catalog per language in i18n/hwio_<lang>.ts:
#   hwio_en.ts  English plural forms only (English is the source language)
#   hwio_cs.ts  full Czech translation
#
# Targets:
#   update_translations  runs lupdate over every backend library and hwio-monitor and rewrites the
#                        committed .ts files (run it after changing user-visible strings).
#   check_translations   runs the same lupdate into a temporary copy and fails when the committed
#                        .ts files are stale; reports the number of unfinished Czech entries.
#                        <location> lines are ignored, so moving code does not make a catalog stale.
#
# The .qm files are embedded into the GUI only (resource prefix /i18n); the CLI is English only.
#
# Adding a language: create i18n/hwio_<lang>.ts (e.g. by copying hwio_cs.ts and changing the language
# attribute), add it to HWIO_TRANSLATED_TS_FILES below and add one entry to
# hwio::i18n::supportedLanguages (libs/core/include/hwio/core/i18n.h).
#
# This file is also run in script mode (cmake -P) by check_translations to compare catalogs.

if(CMAKE_SCRIPT_MODE_FILE)
    # ---- script mode: compare committed vs. regenerated catalogs ------------------------------
    string(REPLACE "," ";" HWIO_TS_NAMES "${HWIO_TS_NAMES}")
    set(_stale "")
    foreach(_name IN LISTS HWIO_TS_NAMES)
        file(READ "${HWIO_COMMITTED_DIR}/${_name}" _committed)
        file(READ "${HWIO_GENERATED_DIR}/check_${_name}" _generated)
        foreach(_var _committed _generated)
            string(REGEX REPLACE "[ \t]*<location[^>]*/>\r?\n" "" ${_var} "${${_var}}")
        endforeach()
        if(NOT _committed STREQUAL _generated)
            list(APPEND _stale "${_name}")
        endif()
    endforeach()

    file(READ "${HWIO_GENERATED_DIR}/check_hwio_cs.ts" _cs)
    string(REGEX MATCHALL "type=\"unfinished\"" _unfinished "${_cs}")
    list(LENGTH _unfinished _unfinished_count)
    message(STATUS "Translations: ${_unfinished_count} unfinished Czech (cs) entries "
                   "(must be 0 before a release, ADR-0027).")

    if(_stale)
        message(FATAL_ERROR
            "Stale translation catalogs: ${_stale}. User-visible strings changed; run "
            "'cmake --build --preset <preset> --target update_translations', translate the new "
            "Czech entries in i18n/hwio_cs.ts (Qt Linguist) and commit the .ts files.")
    endif()
    message(STATUS "Translations: catalogs are up to date.")
    return()
endif()

include_guard(GLOBAL)

set(HWIO_PLURALS_TS_FILE "${PROJECT_SOURCE_DIR}/i18n/hwio_en.ts")
set(HWIO_TRANSLATED_TS_FILES "${PROJECT_SOURCE_DIR}/i18n/hwio_cs.ts")
set(HWIO_LUPDATE_OPTIONS -no-obsolete)

get_property(_hwio_backend_targets GLOBAL PROPERTY HWIO_BACKEND_TARGETS)
# The GUI's QML module and view models live in hwio_gui (the hwio-monitor executable is main.cpp).
set(HWIO_TRANSLATION_SOURCE_TARGETS ${_hwio_backend_targets} hwio_gui)
unset(_hwio_backend_targets)

# update_translations: rewrite the committed catalogs.
qt_add_lupdate(
    LUPDATE_TARGET hwio_lupdate
    SOURCE_TARGETS ${HWIO_TRANSLATION_SOURCE_TARGETS}
    TS_FILES ${HWIO_TRANSLATED_TS_FILES}
    PLURALS_TS_FILE "${HWIO_PLURALS_TS_FILE}"
    OPTIONS ${HWIO_LUPDATE_OPTIONS})

# check_translations: regenerate copies in the build tree and compare.
set(_hwio_check_dir "${PROJECT_BINARY_DIR}/check_translations")
set(_hwio_check_names "")
set(_hwio_check_copy_commands "")
foreach(_ts IN LISTS HWIO_PLURALS_TS_FILE HWIO_TRANSLATED_TS_FILES)
    cmake_path(GET _ts FILENAME _name)
    list(APPEND _hwio_check_names "${_name}")
    list(APPEND _hwio_check_copy_commands
        COMMAND "${CMAKE_COMMAND}" -E copy "${_ts}" "${_hwio_check_dir}/check_${_name}")
endforeach()
add_custom_target(hwio_check_translations_prepare
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_hwio_check_dir}"
    ${_hwio_check_copy_commands}
    COMMENT "Copying translation catalogs for the freshness check"
    VERBATIM)

set(_hwio_check_translated "")
foreach(_ts IN LISTS HWIO_TRANSLATED_TS_FILES)
    cmake_path(GET _ts FILENAME _name)
    list(APPEND _hwio_check_translated "${_hwio_check_dir}/check_${_name}")
endforeach()
cmake_path(GET HWIO_PLURALS_TS_FILE FILENAME _hwio_plurals_name)
qt_add_lupdate(
    LUPDATE_TARGET hwio_check_translations_lupdate
    NO_GLOBAL_TARGET
    SOURCE_TARGETS ${HWIO_TRANSLATION_SOURCE_TARGETS}
    TS_FILES ${_hwio_check_translated}
    PLURALS_TS_FILE "${_hwio_check_dir}/check_${_hwio_plurals_name}"
    OPTIONS ${HWIO_LUPDATE_OPTIONS} -silent)
add_dependencies(hwio_check_translations_lupdate hwio_check_translations_prepare)

list(JOIN _hwio_check_names "," _hwio_check_names_arg)
add_custom_target(check_translations
    COMMAND "${CMAKE_COMMAND}"
        "-DHWIO_TS_NAMES=${_hwio_check_names_arg}"
        "-DHWIO_COMMITTED_DIR=${PROJECT_SOURCE_DIR}/i18n"
        "-DHWIO_GENERATED_DIR=${_hwio_check_dir}"
        -P "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Checking that the translation catalogs are up to date"
    VERBATIM)
add_dependencies(check_translations hwio_check_translations_lupdate)

# Embed the compiled catalogs (resource prefix /i18n) into the GUI library (and so into
# hwio-monitor and tst_gui) and into the libs/core tests, which check the Czech error texts.
qt_add_lrelease(
    LRELEASE_TARGET hwio_lrelease
    TS_FILES "${HWIO_PLURALS_TS_FILE}" ${HWIO_TRANSLATED_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE HWIO_QM_FILES)
foreach(_hwio_target IN ITEMS hwio_gui hwio_core_tests)
    if(TARGET ${_hwio_target})
        qt_add_resources(${_hwio_target} "${_hwio_target}_translations"
            PREFIX "/i18n"
            BASE "${PROJECT_BINARY_DIR}"
            FILES ${HWIO_QM_FILES}
            OUTPUT_TARGETS _hwio_resource_targets)
        hwio_skip_linting(${_hwio_resource_targets})
    endif()
endforeach()
unset(_hwio_target)
unset(_hwio_resource_targets)

unset(_hwio_check_dir)
unset(_hwio_check_names)
unset(_hwio_check_names_arg)
unset(_hwio_check_copy_commands)
unset(_hwio_check_translated)
unset(_hwio_plurals_name)

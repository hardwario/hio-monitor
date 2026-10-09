# Translations (ADR-0027, plan/features/0001-m0-skeleton.md).
#
# One project catalog per language in i18n/hio_<lang>.ts:
#   hio_en.ts  English plural forms only (English is the source language)
#   hio_cs.ts  full Czech translation
#
# Targets:
#   update_translations  runs lupdate over every backend library and hio-monitor and rewrites the
#                        committed .ts files (run it after changing user-visible strings).
#   check_translations   runs the same lupdate into a temporary copy and fails when the committed
#                        .ts files are stale; reports the number of unfinished Czech entries.
#                        <location> lines are ignored, so moving code does not make a catalog stale.
#
# The .qm files are embedded into the GUI only (resource prefix /i18n); the CLI is English only.
#
# Adding a language: create i18n/hio_<lang>.ts (e.g. by copying hio_cs.ts and changing the language
# attribute), add it to HIO_TRANSLATED_TS_FILES below and add one entry to
# hio::i18n::supportedLanguages (libs/core/include/hio/core/i18n.h).
#
# This file is also run in script mode (cmake -P) by check_translations to compare catalogs.

if(CMAKE_SCRIPT_MODE_FILE)
    # ---- script mode: compare committed vs. regenerated catalogs ------------------------------
    string(REPLACE "," ";" HIO_TS_NAMES "${HIO_TS_NAMES}")
    set(_stale "")
    foreach(_name IN LISTS HIO_TS_NAMES)
        file(READ "${HIO_COMMITTED_DIR}/${_name}" _committed)
        file(READ "${HIO_GENERATED_DIR}/check_${_name}" _generated)
        foreach(_var _committed _generated)
            string(REGEX REPLACE "[ \t]*<location[^>]*/>\r?\n" "" ${_var} "${${_var}}")
        endforeach()
        if(NOT _committed STREQUAL _generated)
            list(APPEND _stale "${_name}")
        endif()
    endforeach()

    file(READ "${HIO_GENERATED_DIR}/check_hio_cs.ts" _cs)
    string(REGEX MATCHALL "type=\"unfinished\"" _unfinished "${_cs}")
    list(LENGTH _unfinished _unfinished_count)
    message(STATUS "Translations: ${_unfinished_count} unfinished Czech (cs) entries "
                   "(must be 0 before a release, ADR-0027).")

    if(_stale)
        message(FATAL_ERROR
            "Stale translation catalogs: ${_stale}. User-visible strings changed; run "
            "'cmake --build --preset <preset> --target update_translations', translate the new "
            "Czech entries in i18n/hio_cs.ts (Qt Linguist) and commit the .ts files.")
    endif()
    message(STATUS "Translations: catalogs are up to date.")
    return()
endif()

include_guard(GLOBAL)

set(HIO_PLURALS_TS_FILE "${PROJECT_SOURCE_DIR}/i18n/hio_en.ts")
set(HIO_TRANSLATED_TS_FILES "${PROJECT_SOURCE_DIR}/i18n/hio_cs.ts")
set(HIO_LUPDATE_OPTIONS -no-obsolete)

get_property(_hio_backend_targets GLOBAL PROPERTY HIO_BACKEND_TARGETS)
# The GUI's QML module and view models live in hio_gui (the hio-monitor executable is main.cpp).
set(HIO_TRANSLATION_SOURCE_TARGETS ${_hio_backend_targets} hio_gui)
unset(_hio_backend_targets)

# update_translations: rewrite the committed catalogs.
qt_add_lupdate(
    LUPDATE_TARGET hio_lupdate
    SOURCE_TARGETS ${HIO_TRANSLATION_SOURCE_TARGETS}
    TS_FILES ${HIO_TRANSLATED_TS_FILES}
    PLURALS_TS_FILE "${HIO_PLURALS_TS_FILE}"
    OPTIONS ${HIO_LUPDATE_OPTIONS})

# check_translations: regenerate copies in the build tree and compare.
set(_hio_check_dir "${PROJECT_BINARY_DIR}/check_translations")
set(_hio_check_names "")
set(_hio_check_copy_commands "")
foreach(_ts IN LISTS HIO_PLURALS_TS_FILE HIO_TRANSLATED_TS_FILES)
    cmake_path(GET _ts FILENAME _name)
    list(APPEND _hio_check_names "${_name}")
    list(APPEND _hio_check_copy_commands
        COMMAND "${CMAKE_COMMAND}" -E copy "${_ts}" "${_hio_check_dir}/check_${_name}")
endforeach()
add_custom_target(hio_check_translations_prepare
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_hio_check_dir}"
    ${_hio_check_copy_commands}
    COMMENT "Copying translation catalogs for the freshness check"
    VERBATIM)

set(_hio_check_translated "")
foreach(_ts IN LISTS HIO_TRANSLATED_TS_FILES)
    cmake_path(GET _ts FILENAME _name)
    list(APPEND _hio_check_translated "${_hio_check_dir}/check_${_name}")
endforeach()
cmake_path(GET HIO_PLURALS_TS_FILE FILENAME _hio_plurals_name)
qt_add_lupdate(
    LUPDATE_TARGET hio_check_translations_lupdate
    NO_GLOBAL_TARGET
    SOURCE_TARGETS ${HIO_TRANSLATION_SOURCE_TARGETS}
    TS_FILES ${_hio_check_translated}
    PLURALS_TS_FILE "${_hio_check_dir}/check_${_hio_plurals_name}"
    OPTIONS ${HIO_LUPDATE_OPTIONS} -silent)
add_dependencies(hio_check_translations_lupdate hio_check_translations_prepare)

list(JOIN _hio_check_names "," _hio_check_names_arg)
add_custom_target(check_translations
    COMMAND "${CMAKE_COMMAND}"
        "-DHIO_TS_NAMES=${_hio_check_names_arg}"
        "-DHIO_COMMITTED_DIR=${PROJECT_SOURCE_DIR}/i18n"
        "-DHIO_GENERATED_DIR=${_hio_check_dir}"
        -P "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Checking that the translation catalogs are up to date"
    VERBATIM)
add_dependencies(check_translations hio_check_translations_lupdate)

# Embed the compiled catalogs (resource prefix /i18n) into the GUI library (and so into
# hio-monitor and tst_gui) and into the libs/core tests, which check the Czech error texts.
qt_add_lrelease(
    LRELEASE_TARGET hio_lrelease
    TS_FILES "${HIO_PLURALS_TS_FILE}" ${HIO_TRANSLATED_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE HIO_QM_FILES)
foreach(_hio_target IN ITEMS hio_gui hio_core_tests)
    if(TARGET ${_hio_target})
        qt_add_resources(${_hio_target} "${_hio_target}_translations"
            PREFIX "/i18n"
            BASE "${PROJECT_BINARY_DIR}"
            FILES ${HIO_QM_FILES}
            OUTPUT_TARGETS _hio_resource_targets)
        hio_skip_linting(${_hio_resource_targets})
    endif()
endforeach()
unset(_hio_target)
unset(_hio_resource_targets)

unset(_hio_check_dir)
unset(_hio_check_names)
unset(_hio_check_names_arg)
unset(_hio_check_copy_commands)
unset(_hio_check_translated)
unset(_hio_plurals_name)

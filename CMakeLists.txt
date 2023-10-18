cmake_minimum_required(VERSION 3.21.1)

set(BUILD_QDS_COMPONENTS ON CACHE BOOL "Build design studio components")

project(hmonitor LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 COMPONENTS Gui Qml Quick Bluetooth)
find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick Bluetooth)
qt_standard_project_setup()

if (Qt6_VERSION VERSION_LESS 6.5.0)
message(FATAL_ERROR "You need Qt 6.5.0 or newer to build the application.")
endif()

set(APP_VERSION "")

set(PROJECT_SOURCES
    src/main.cpp
    src/searchhighlighter.h
    src/searchhighlighter.cpp
    src/searchcomponent.h
    src/searchcomponent.cpp
    src/chester.h
    src/chester.cpp
    src/historyfile.h
    src/historyfile.cpp
    src/bluetooth.h
    src/bluetooth.cpp
    src/bluetoothworker.h
    src/bluetoothworker.cpp
    src/deviceinfo.h
    src/deviceinfo.cpp
    src/messagemodel.h
    src/messagemodel.cpp
    src/devicemodel.h
    src/devicemodel.cpp
    src/flash.h
    src/flash.cpp
    src/filedownloader.h
    src/filedownloader.cpp
    src/deviceInterface.h
)

if(WIN32)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/ci/windows/hiomon.rc")
    qt_add_executable(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCES} ${app_icon_resource_windows})
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE lib/jlink/windows)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE JLink_x64)
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE lib/nrfjprog/windows)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE nrfjprog)
elseif(APPLE)
    set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.icns")
    set_source_files_properties(${app_icon_macos} PROPERTIES
              MACOSX_PACKAGE_LOCATION "Resources")
    qt_add_executable(${CMAKE_PROJECT_NAME} MACOSX_BUNDLE ${PROJECT_SOURCES} ${app_icon_macos})
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE lib/jlink/macos)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE jlinkarm)
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE lib/nrfjprog/macos)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE nrfjprogdll)
elseif(UNIX)
    qt_add_executable(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCES})
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE lib/jlink/linux)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE jlinkarm)
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE lib/nrfjprog/linux)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE nrfjprogdll)
endif()

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE APP_VERSION="${APP_VERSION}")

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    AUTOMOC TRUE
    AUTOUIC TRUE
    AUTORCC TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE icons.icns
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ci/macos/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.hardwario.hiomon
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
    OUTPUT_NAME "HARDWARIO Monitor"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/jlink
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nrfjprog
)

qt_add_resources(${CMAKE_PROJECT_NAME} "configuration"
    PREFIX "/"
    FILES
        qtquickcontrols2.conf
)

qt_add_resources(${CMAKE_PROJECT_NAME} "res"
    PREFIX "/"
    FILES
        qml.qrc
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::Bluetooth
)

if (${BUILD_QDS_COMPONENTS})
    include(${CMAKE_CURRENT_SOURCE_DIR}/qmlcomponents)
endif ()

include(${CMAKE_CURRENT_SOURCE_DIR}/qmlmodules)

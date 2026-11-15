cmake_minimum_required(VERSION 3.22)
project(toffee VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VERBOSE_LOGGING "Include startup/runtime logging" OFF)
option(TOFFEE_APP_DIAGNOSTICS "Include UI and live-stream maintenance commands" OFF)
include(cmake/LocalConfig.cmake OPTIONAL)

if(NOT MINGW)
    message(FATAL_ERROR "Toffee builds with MinGW only (the compiler the static Qt was built with). Use .\\build.ps1.")
endif()
find_package(Qt6 6.6 REQUIRED COMPONENTS Quick QuickControls2 SerialPort Network)
get_target_property(_qt_core_type Qt6::Core TYPE)
if(NOT _qt_core_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Toffee needs the static Qt (found a shared one in ${Qt6_DIR}). "
                        "Run tools/fetch-qt-static.ps1 and point CMAKE_PREFIX_PATH at C:/qt-static.")
endif()
qt_standard_project_setup(REQUIRES 6.5)
enable_language(RC)
include(cmake/Version.cmake)

set(TOFFEE_WARNINGS -Wall -Wextra -Wpedantic)

qt_add_library(toffee_core STATIC
    src/audio/audio_capture.cpp src/audio/audio_capture.h src/audio/wasapi_guids.cpp
    src/dsu/dsu_server.cpp src/dsu/dsu_server.h
    src/inputs/input_controller.cpp src/inputs/input_controller.h src/inputs/gamepad_buttons.h
    src/inputs/virtual_controller.cpp src/inputs/virtual_controller.h
    src/logger/session_log.cpp src/logger/session_log.h
    src/logger/toffee_log.cpp src/logger/toffee_log.h
    src/stream/encoder_session.cpp src/stream/encoder_session.h
    src/stream/bridge_decoder.cpp src/stream/bridge_decoder.h
    src/stream/bridge_link.cpp src/stream/bridge_link.h
    src/stream/capture_model.cpp src/stream/capture_model.h
    src/stream/protocol.cpp src/stream/protocol.h
    src/stream/stream_delivery.cpp src/stream/stream_session.cpp
    src/stream/stream_controller.cpp src/stream/stream_controller.h src/stream/stream_state.h
    src/transport/device_transport.cpp src/transport/device_transport.h
    src/transport/native_serial_writer.cpp src/transport/native_serial_writer.h
    src/transport/serial_write_queue.cpp src/transport/serial_write_queue.h
    src/video/capture_converter.cpp src/video/capture_converter.h
    src/video/capture_source.cpp src/video/capture_source.h
    src/video/native_capture.cpp src/video/native_capture.h
    src/video/scene.cpp src/video/scene.h
)
qt_extract_metatypes(toffee_core)
target_compile_features(toffee_core PUBLIC cxx_std_17)
target_include_directories(toffee_core PUBLIC src "${TOFFEE_GENERATED_DIR}")
target_compile_definitions(toffee_core PRIVATE VERBOSE_LOGGING=$<BOOL:${VERBOSE_LOGGING}>)
target_compile_options(toffee_core PRIVATE ${TOFFEE_WARNINGS})
set_source_files_properties(src/video/capture_converter.cpp PROPERTIES COMPILE_OPTIONS -O2)
target_link_libraries(toffee_core PUBLIC
    Qt6::Gui Qt6::SerialPort Qt6::Network vigemclient
    d3d11 dxgi dxguid ole32 uuid winmm dwmapi)
include(cmake/Encoder.cmake)

qt_add_executable(toffee WIN32 src/main.cpp src/app/application.cpp src/app/application.h)
set(_qml_files
    src/ui/qml/Main.qml
    src/ui/qml/Panel.qml
    src/ui/qml/ActionButton.qml
    src/ui/qml/FormRow.qml
    src/ui/qml/SelectBox.qml
    src/ui/qml/FlashPage.qml
    src/ui/qml/ConnectPage.qml
    src/ui/qml/InputsPage.qml
    src/ui/qml/SettingsPage.qml
    src/ui/qml/SecondScreen.qml
    src/ui/qml/StickPreview.qml
)
foreach(_qml_file IN LISTS _qml_files)
    get_filename_component(_qml_name "${_qml_file}" NAME)
    set_source_files_properties("${_qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${_qml_name}")
endforeach()
qt_add_qml_module(toffee
    URI Toffee
    VERSION 1.0
    QML_FILES ${_qml_files}
    SOURCES src/app/qml_types.h
        src/app/frontend_controller.cpp src/app/frontend_controller.h
        src/display/virtual_display.cpp src/display/virtual_display.h
        src/firmware/firmware_flasher.cpp src/firmware/firmware_flasher.h
        src/video/scene_preview.cpp src/video/scene_preview.h
)
if(TOFFEE_APP_DIAGNOSTICS)
    target_sources(toffee PRIVATE src/app/diagnostics.cpp src/app/diagnostics.h)
endif()
target_include_directories(toffee PRIVATE src/app src/display src/firmware src/video)
target_compile_definitions(toffee PRIVATE TOFFEE_APP_DIAGNOSTICS=$<BOOL:${TOFFEE_APP_DIAGNOSTICS}>)
target_compile_options(toffee PRIVATE ${TOFFEE_WARNINGS})
target_link_options(toffee PRIVATE -static)
target_link_libraries(toffee PRIVATE toffee_core Qt6::Quick Qt6::QuickControls2)
toffee_add_windows_resource(toffee "Toffee: stream your PC to a Wii U GamePad" "Toffee.exe")
include(cmake/ViGEm.cmake)
include(cmake/DisplayDriver.cmake)
include(cmake/FirmwareBundle.cmake)

qt_add_executable(toffee-cli src/cli/main.cpp)
target_compile_options(toffee-cli PRIVATE ${TOFFEE_WARNINGS})
target_link_options(toffee-cli PRIVATE -static)
target_link_libraries(toffee-cli PRIVATE toffee_core)
toffee_add_windows_resource(toffee-cli "Toffee command line" "toffee-cli.exe")

add_custom_target(dist ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:toffee>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/Toffee.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:toffee-cli>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/toffee-cli.exe"
    DEPENDS toffee toffee-cli
    COMMENT "Staging Toffee.exe and toffee-cli.exe in dist/"
    VERBATIM
)

enable_testing()
add_executable(protocol_test EXCLUDE_FROM_ALL tests/bridge_protocol_test.cpp src/stream/bridge_decoder.cpp)
target_compile_features(protocol_test PRIVATE cxx_std_17)
target_include_directories(protocol_test PRIVATE src firmware/src/transport)
target_compile_options(protocol_test PRIVATE ${TOFFEE_WARNINGS})
set_target_properties(protocol_test PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME build_protocol_test COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target protocol_test)
add_test(NAME protocol COMMAND protocol_test)
set_tests_properties(protocol PROPERTIES DEPENDS build_protocol_test)
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure DEPENDS protocol_test
                  COMMENT "Running the host test" VERBATIM)

file(GLOB_RECURSE TOFFEE_CPP_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/main/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/main/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)
file(GLOB_RECURSE TOFFEE_PC_CPP_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

set(TOFFEE_CLANG_FORMAT_EXECUTABLE "" CACHE FILEPATH "Path to clang-format")
if(NOT TOFFEE_CLANG_FORMAT_EXECUTABLE)
    find_program(TOFFEE_CLANG_FORMAT_EXECUTABLE NAMES clang-format)
endif()
if(TOFFEE_CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND "${TOFFEE_CLANG_FORMAT_EXECUTABLE}" -i ${TOFFEE_CPP_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting Toffee C++ sources"
        VERBATIM
    )
    add_custom_target(format_check
        COMMAND "${TOFFEE_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${TOFFEE_CPP_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking Toffee C++ formatting"
        VERBATIM
    )
endif()

set(TOFFEE_CLANG_TIDY_EXECUTABLE "" CACHE FILEPATH "Path to clang-tidy")
if(NOT TOFFEE_CLANG_TIDY_EXECUTABLE)
    find_program(TOFFEE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
endif()
if(TOFFEE_CLANG_TIDY_EXECUTABLE)
    add_custom_target(tidy
        COMMAND "${TOFFEE_CLANG_TIDY_EXECUTABLE}" ${TOFFEE_PC_CPP_FILES} -p "${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-tidy on PC C++ sources"
        VERBATIM
    )
endif()

add_custom_target(lint DEPENDS toffee all_qmllint)
if(TARGET tidy)
    add_dependencies(lint tidy)
endif()

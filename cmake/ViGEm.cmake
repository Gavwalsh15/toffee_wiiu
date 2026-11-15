set(_vigem_source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ViGEmClient")
if(NOT EXISTS "${_vigem_source}/src/ViGEmClient.cpp")
    message(FATAL_ERROR
        "third_party/ViGEmClient is missing. Initialise it with: git submodule update --init --recursive")
endif()

add_library(vigemclient STATIC "${_vigem_source}/src/ViGEmClient.cpp")
set_target_properties(vigemclient PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(vigemclient PUBLIC "${_vigem_source}/include")
target_compile_features(vigemclient PRIVATE cxx_std_17)
target_compile_options(vigemclient PRIVATE -w)
target_link_libraries(vigemclient PUBLIC setupapi)

set(_vigembus_version "1.22.0")
set(_vigembus_installer "${CMAKE_BINARY_DIR}/vigembus/ViGEmBus_setup.exe")
if(NOT EXISTS "${_vigembus_installer}")
    message(STATUS "Downloading the ViGEmBus ${_vigembus_version} installer")
    file(DOWNLOAD
        "https://github.com/nefarius/ViGEmBus/releases/download/v${_vigembus_version}/ViGEmBus_${_vigembus_version}_x64_x86_arm64.exe"
        "${_vigembus_installer}"
        EXPECTED_HASH SHA256=89220a7865076b342892f98865f3499fb7c4cfd673159e89d352c360fd014c6a
        STATUS _vigembus_status)
    list(GET _vigembus_status 0 _vigembus_code)
    if(NOT _vigembus_code EQUAL 0)
        file(REMOVE "${_vigembus_installer}")
        message(FATAL_ERROR "Could not download the ViGEmBus installer: ${_vigembus_status}")
    endif()
endif()
set_source_files_properties("${_vigembus_installer}" PROPERTIES QT_RESOURCE_ALIAS "ViGEmBus_setup.exe")
qt_add_resources(toffee vigembus_driver PREFIX "/driver" FILES "${_vigembus_installer}")

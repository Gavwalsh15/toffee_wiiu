cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
    message(FATAL_ERROR "Open an ESP-IDF environment before configuring firmware.")
endif()

# Toffee's ESP-IDF changes (Nintendo pairing in wpa_supplicant, USB receive backpressure) live in
# patches/esp-idf-*.patch and are applied to the pinned submodule before ESP-IDF is loaded. Only
# the submodule is patched, never an ESP-IDF installed elsewhere.
get_filename_component(_idf "$ENV{IDF_PATH}" REALPATH)
get_filename_component(_pinned_idf "${CMAKE_CURRENT_LIST_DIR}/../third_party/esp-idf" REALPATH)
if(NOT _idf STREQUAL _pinned_idf)
    message(FATAL_ERROR "IDF_PATH is ${_idf}, but the firmware builds against the patched "
                        "third_party/esp-idf. Use tools/package-firmware.ps1 or tools/build-firmware.sh.")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ApplyPatch.cmake")
file(GLOB _idf_patches "${CMAKE_CURRENT_LIST_DIR}/../patches/esp-idf-*.patch")
foreach(_patch IN LISTS _idf_patches)
    toffee_apply_patch("${_pinned_idf}" "${_patch}")
endforeach()

if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")
endif()
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
target_sources(__idf_wpa_supplicant PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/wpa_supplicant/nintendo_pairing.c")
target_include_directories(__idf_wpa_supplicant PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/wpa_supplicant")
target_compile_definitions(__idf_wpa_supplicant PRIVATE TOFFEE_NINTENDO_PAIRING=1)

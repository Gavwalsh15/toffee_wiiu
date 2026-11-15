set(_vdd_version "25.7.23")
set(_vdd_dir "${CMAKE_BINARY_DIR}/display-driver")
set(_vdd_zip "${_vdd_dir}/VirtualDisplayDriver.zip")
if(NOT EXISTS "${_vdd_dir}/VirtualDisplayDriver/MttVDD.inf")
    message(STATUS "Downloading the Virtual Display Driver ${_vdd_version}")
    file(DOWNLOAD
        "https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/download/${_vdd_version}/VirtualDisplayDriver-x86.Driver.Only.zip"
        "${_vdd_zip}"
        EXPECTED_HASH SHA256=e24210692b442b39af763536330ce78b423f19342b7a7792c26de3944e418b3a
        STATUS _vdd_status)
    list(GET _vdd_status 0 _vdd_code)
    if(NOT _vdd_code EQUAL 0)
        file(REMOVE "${_vdd_zip}")
        message(FATAL_ERROR "Could not download the Virtual Display Driver: ${_vdd_status}")
    endif()
    # Despite the file name, this package holds the 64-bit driver.
    file(ARCHIVE_EXTRACT INPUT "${_vdd_zip}" DESTINATION "${_vdd_dir}")
endif()

set(_vdd_files
    "${_vdd_dir}/VirtualDisplayDriver/MttVDD.inf"
    "${_vdd_dir}/VirtualDisplayDriver/MttVDD.dll"
    "${_vdd_dir}/VirtualDisplayDriver/mttvdd.cat"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/display/vdd_settings.xml")
foreach(_vdd_file IN LISTS _vdd_files)
    get_filename_component(_vdd_name "${_vdd_file}" NAME)
    set_source_files_properties("${_vdd_file}" PROPERTIES QT_RESOURCE_ALIAS "${_vdd_name}")
endforeach()
qt_add_resources(toffee display_driver PREFIX "/display-driver" FILES ${_vdd_files})
target_sources(toffee PRIVATE src/display/driver_installer.cpp src/display/driver_installer.h)
target_link_libraries(toffee PRIVATE setupapi newdev)

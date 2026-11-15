# The live encoder: encoder.exe, built from the patched drc-x264. Both executables embed it as
# resource 101 (see cmake/Version.cmake) and unpack it when a stream starts.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/drc-x264/configure")
    message(FATAL_ERROR "third_party/drc-x264 is missing. Initialise it with: git submodule update --init --recursive")
endif()
include(cmake/X264.cmake)

add_executable(encoder src/encoder/live_encoder.cpp)
set_target_properties(encoder PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(TOFFEE_ENCODER_EXE "${CMAKE_CURRENT_BINARY_DIR}/encoder.exe")
# The MinGW this project pins (13.1) lacks a time function the encoder uses.
target_sources(encoder PRIVATE src/encoder/mingw_time_compat.cpp)
target_compile_features(encoder PRIVATE cxx_std_17)
target_include_directories(encoder PRIVATE src)
target_link_libraries(encoder PRIVATE x264 winpthread)
target_compile_options(encoder PRIVATE -O2)
target_link_options(encoder PRIVATE -static)

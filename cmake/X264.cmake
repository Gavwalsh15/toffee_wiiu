include(cmake/ApplyPatch.cmake)

set(_x264_source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/drc-x264")
toffee_apply_patch("${_x264_source}" "${CMAKE_CURRENT_SOURCE_DIR}/patches/drc-x264-gamepad.patch")

enable_language(C)
set(_x264_sources
    common/mc.c common/predict.c common/pixel.c common/macroblock.c common/frame.c common/dct.c
    common/cpu.c common/cabac.c common/common.c common/osdep.c common/rectangle.c common/set.c
    common/quant.c common/deblock.c common/vlc.c common/mvpred.c common/bitstream.c
    common/threadpool.c
    encoder/analyse.c encoder/me.c encoder/ratecontrol.c encoder/set.c encoder/macroblock.c
    encoder/cabac.c encoder/cavlc.c encoder/encoder.c encoder/lookahead.c)
list(TRANSFORM _x264_sources PREPEND "${_x264_source}/")

file(GLOB _x264_nasm_dirs "${CMAKE_CURRENT_SOURCE_DIR}/.tools/nasm/nasm-*")
find_program(TOFFEE_NASM nasm HINTS ${_x264_nasm_dirs})
if(NOT TOFFEE_NASM)
    message(FATAL_ERROR "NASM was not found. x264 needs its assembly to encode in real time. "
                        "Run build.ps1, which downloads it, or install NASM.")
endif()
set(CMAKE_ASM_NASM_COMPILER "${TOFFEE_NASM}")
enable_language(ASM_NASM)
# The x86-64 lists from x264's Makefile.
set(_x264_asm
    const-a cabac-a dct-a deblock-a mc-a mc-a2 pixel-a predict-a quant-a cpu-a dct-64
    bitstream-a sad-a trellis-64)
list(TRANSFORM _x264_asm PREPEND "${_x264_source}/common/x86/")
list(TRANSFORM _x264_asm APPEND ".asm")
list(APPEND _x264_sources ${_x264_asm}
    "${_x264_source}/common/x86/mc-c.c" "${_x264_source}/common/x86/predict-c.c")

add_library(x264 STATIC ${_x264_sources})
# The project turns Qt's code generation on globally; plain C needs none of it.
set_target_properties(x264 PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
# Our config.h and x264_config.h come first; the submodule provides x264.h and the sources.
target_include_directories(x264 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/cmake/x264" "${_x264_source}")
target_compile_definitions(x264 PRIVATE HAVE_MMX=1)
set(_x264_c_flags
    -std=gnu99 -O3 -ffast-math -fomit-frame-pointer -fno-tree-vectorize -m64 -w
    # x264 relies on assert(); keep it active even in a Release build, as the autotools build does.
    -UNDEBUG)
# The flags configure passes to NASM. The include path needs its trailing slash.
set(_x264_nasm_flags
    -DARCH_X86_64=1 -DHAVE_ALIGNED_STACK=1 -DHIGH_BIT_DEPTH=0 -DBIT_DEPTH=8
    "-I${_x264_source}/common/x86/" -w-pp-macro-params-legacy)
target_compile_options(x264 PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:${_x264_c_flags}>" "$<$<COMPILE_LANGUAGE:ASM_NASM>:${_x264_nasm_flags}>")

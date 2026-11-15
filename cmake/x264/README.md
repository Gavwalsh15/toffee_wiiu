# x264 configuration

`config.h` and `x264_config.h` are the two files x264's autotools `configure` would generate for a
static 8-bit x86-64 MinGW build with OpenCL turned off. The build compiles
`third_party/drc-x264` directly from `cmake/X264.cmake` with these headers, so it needs no `sh`,
`make` or `perl`. The submodule is pinned, so they only change if the pin moves.

The SIMD assembly is assembled with NASM (`tools/fetch-nasm.ps1` downloads it into `.tools/nasm`)
and makes the encoder about six times faster, so the build requires it. The fork's assembly was written for yasm, so `drc-x264-gamepad.patch` adds a small NASM shim to
`common/x86/x86inc.asm`.

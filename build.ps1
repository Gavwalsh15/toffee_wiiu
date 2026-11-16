param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $QtPrefixPath = '',
    [switch] $VerboseLogging,
    [switch] $AppDiagnostics,
    [switch] $Fresh,
    [switch] $RebuildFirmware,
    [switch] $QmlLint,
    # Run the host tests (tests/) after the build.
    [switch] $Test,
    # A release version such as v1.2.3 or v1.2.3-rc1 (default: the newest v* tag plus -dev).
    [string] $Version = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$toolchain = & (Join-Path $root 'tools\find-toolchain.ps1')
if (-not $toolchain.CMake) { throw 'No usable Windows CMake found. Install CMake 3.22+, or run the firmware build once (it installs one in .tools).' }
if (-not $toolchain.Ninja) { throw 'Ninja was not found. Install it and try again.' }
if (-not $toolchain.MinGWBin) { throw "MinGW $($toolchain.MinGWVersion) was not found. Install the Qt MinGW toolchain (C:\Qt\Tools)." }
$CxxCompiler = Join-Path $toolchain.MinGWBin 'g++.exe'
# The compiler and Ninja must win over anything else on PATH, such as a different MinGW.
$env:PATH = "$(Split-Path -Parent $CxxCompiler);$(Split-Path -Parent $toolchain.Ninja);$env:PATH"

if (-not $QtPrefixPath) {
    foreach ($candidate in 'C:\qt-static', (Join-Path $root 'build\qt-static\install')) {
        if (Test-Path (Join-Path $candidate 'lib\cmake\Qt6\Qt6Config.cmake')) { $QtPrefixPath = $candidate; break }
    }
}
if (-not $QtPrefixPath) {
    & (Join-Path $root 'tools\fetch-qt-static.ps1')
    $QtPrefixPath = 'C:\qt-static'
}

foreach ($module in @(@{ Path = 'third_party/drc-x264'; Marker = 'configure' }, @{ Path = 'third_party/ViGEmClient'; Marker = 'src\ViGEmClient.cpp' })) {
    if (-not (Test-Path (Join-Path $root "$($module.Path)\$($module.Marker)"))) {
        git -C $root submodule update --init $module.Path
        if ($LASTEXITCODE -ne 0) { throw "Could not fetch $($module.Path). Clone with --recurse-submodules." }
    }
}

if (-not (Get-Command nasm -ErrorAction SilentlyContinue)) {
    & (Join-Path $root 'tools\fetch-nasm.ps1')
}

if ($RebuildFirmware -or -not (Test-Path (Join-Path $root 'build\firmware-app\manifest.json'))) {
    # The ESP-IDF build puts its tools first on PATH, and their DLLs crash Qt's qmlimportscanner.
    $path = $env:PATH
    & (Join-Path $root 'tools\package-firmware.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
    $env:PATH = $path
}

$buildDirectory = Join-Path $root 'build\pc'
$configureArguments = @()
if ($Fresh) { $configureArguments += '--fresh' }
$configureArguments += @(
    '-S', $root, '-B', $buildDirectory, '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DVERBOSE_LOGGING=$(if ($VerboseLogging) { 'ON' } else { 'OFF' })",
    "-DTOFFEE_APP_DIAGNOSTICS=$(if ($AppDiagnostics -or $Configuration -eq 'Debug') { 'ON' } else { 'OFF' })",
    "-DTOFFEE_VERSION=$Version",
    "-DCMAKE_CXX_COMPILER=$($CxxCompiler -replace '\\', '/')",
    "-DCMAKE_C_COMPILER=$((Join-Path (Split-Path -Parent $CxxCompiler) 'gcc.exe') -replace '\\', '/')",
    "-DCMAKE_PREFIX_PATH=$($QtPrefixPath -replace '\\', '/')"
)

if (Get-Command ccache -ErrorAction SilentlyContinue) {
    $configureArguments += @('-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache')
} else {
    # Forget a ccache an earlier run found, for example one the ESP-IDF tools put on PATH.
    $configureArguments += @('-UCMAKE_C_COMPILER_LAUNCHER', '-UCMAKE_CXX_COMPILER_LAUNCHER')
}

& $toolchain.CMake @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'Toffee configuration failed.' }
if ($QmlLint) {
    & $toolchain.CMake --build $buildDirectory --config $Configuration --target all_qmllint
    if ($LASTEXITCODE -ne 0) { throw 'Toffee QML lint failed.' }
}
& $toolchain.CMake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Toffee build failed.' }

if ($Test) {
    & $toolchain.CMake --build $buildDirectory --config $Configuration --target check
    if ($LASTEXITCODE -ne 0) { throw 'Toffee tests failed.' }
}

$artifact = Join-Path $root 'dist\Toffee.exe'
if (-not (Test-Path -LiteralPath $artifact)) { throw "Build completed but the app is missing: $artifact" }
Write-Host "Built successfully: $artifact"

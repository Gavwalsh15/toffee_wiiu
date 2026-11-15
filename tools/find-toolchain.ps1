# Finds the build tools: a native Windows CMake (on PATH, in the Qt tools folder, or the one the
# firmware build installs in .tools), Ninja (on PATH or in the Qt tools), and the MinGW pinned in
# toolchain.json, which comes from the Qt tools (C:\Qt\Tools, or IQTA_TOOLS in CI).
$ErrorActionPreference = 'Stop'
$pins = Get-Content (Join-Path (Split-Path -Parent $PSScriptRoot) 'toolchain.json') -Raw | ConvertFrom-Json
$qtTools = @($env:IQTA_TOOLS, 'C:\Qt\Tools') | Where-Object { $_ -and (Test-Path $_) }

$repoRoot = Split-Path -Parent $PSScriptRoot
$cmake = @(Get-Command cmake -All -ErrorAction SilentlyContinue | ForEach-Object Source) +
         @($qtTools | ForEach-Object { Join-Path $_ 'CMake_64\bin\cmake.exe' }) +
         @(Get-ChildItem (Join-Path $repoRoot '.tools\idf-tools\tools\cmake\*\bin\cmake.exe') -ErrorAction SilentlyContinue | ForEach-Object FullName) |
    Where-Object { $_ -and (Test-Path -LiteralPath $_) -and ((& $_ -E capabilities | Out-String) -match 'MinGW Makefiles') } |
    Select-Object -First 1

$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
if (-not $ninja) {
    $ninja = $qtTools | ForEach-Object { Join-Path $_ 'Ninja\ninja.exe' } | Where-Object { Test-Path $_ } | Select-Object -First 1
}

$mingw = $qtTools | ForEach-Object { Get-ChildItem (Join-Path $_ "$($pins.mingw.qtTool)_64\bin") -Filter g++.exe -ErrorAction SilentlyContinue } |
    Select-Object -First 1

[pscustomobject]@{
    CMake        = $cmake
    Ninja        = $ninja
    MinGWBin     = $mingw.DirectoryName
    MinGWVersion = $pins.mingw.version
}

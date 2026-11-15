param(
    [string]$Prefix = 'C:\qt-static',
    [string]$WorkRoot = 'C:\q',
    [string]$MinGWBin = ''
)

$ErrorActionPreference = 'Stop'
$pins = Get-Content (Join-Path $PSScriptRoot '..\toolchain.json') -Raw | ConvertFrom-Json
$version = $pins.qt.version
$archive = "qt-everywhere-src-$version.tar.xz"
$url = "https://download.qt.io/official_releases/qt/$($version -replace '\.\d+$', '')/$version/single/$archive"
$sha256 = $pins.qt.sourceSha256

if (Test-Path (Join-Path $Prefix 'lib/cmake/Qt6/Qt6Config.cmake')) {
    Write-Host "Static Qt $version already built in $Prefix"
    return
}
if ($MinGWBin) { $env:PATH = "$MinGWBin;$env:PATH" }
foreach ($tool in 'gcc', 'g++', 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool was not found on PATH." }
}

New-Item -ItemType Directory -Force $WorkRoot | Out-Null
$archivePath = Join-Path $WorkRoot $archive
if (-not (Test-Path $archivePath)) {
    Write-Host "Downloading $archive (1.3 GB)"
    Invoke-WebRequest $url -OutFile $archivePath
}
if ((Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLower() -ne $sha256) {
    Remove-Item $archivePath
    throw "$archive does not match the expected SHA-256."
}
$source = Join-Path $WorkRoot "qt-everywhere-src-$version"
if (-not (Test-Path (Join-Path $source 'configure.bat'))) {
    Write-Host 'Extracting the Qt source'
    & "$env:SystemRoot\System32\tar.exe" -xf $archivePath -C $WorkRoot
    if ($LASTEXITCODE -ne 0) { throw 'Could not extract the Qt source.' }
}

$build = Join-Path $WorkRoot 'build'
New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $build
try {
    Write-Host 'Configuring Qt'
    & (Join-Path $source 'configure.bat') -prefix $Prefix -release -static -static-runtime `
        -opensource -confirm-license -nomake examples -nomake tests -cmake-generator Ninja `
        -submodules 'qtbase,qtdeclarative,qtmultimedia,qtserialport,qtsvg,qtshadertools'
    if ($LASTEXITCODE -ne 0) { throw 'Qt configure failed.' }
    Write-Host 'Building Qt (this takes a while)'
    cmake --build . --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Qt build failed.' }
    cmake --install .
    if ($LASTEXITCODE -ne 0) { throw 'Qt install failed.' }
} finally {
    Pop-Location
}
Write-Host "Static Qt $version installed in $Prefix"

param(
    [string]$Prefix = 'C:\qt-static',
    [string]$Repository = $(if ($env:GITHUB_REPOSITORY) { $env:GITHUB_REPOSITORY } else { 'Gavwalsh15/toffee_wiiu' })
)

$ErrorActionPreference = 'Stop'

function Expand-TarXz([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force $Destination | Out-Null
    $sevenZip = (Get-Command 7z -ErrorAction SilentlyContinue).Source
    if (-not $sevenZip -and (Test-Path 'C:\Program Files\7-Zip\7z.exe')) { $sevenZip = 'C:\Program Files\7-Zip\7z.exe' }
    if (-not $sevenZip) {
        & "$env:SystemRoot\System32\tar.exe" -xf $Archive -C $Destination
        if ($LASTEXITCODE -ne 0) { throw "Could not extract $Archive. Install 7-Zip and try again." }
        return
    }
    $folder = Split-Path -Parent $Archive
    $tar = Join-Path $folder ([IO.Path]::GetFileNameWithoutExtension($Archive))
    & $sevenZip x $Archive "-o$folder" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7-Zip could not decompress $Archive." }
    & $sevenZip x $tar "-o$Destination" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7-Zip could not extract $tar." }
    Remove-Item $tar -Force
}
$pins = Get-Content (Join-Path $PSScriptRoot '..\toolchain.json') -Raw | ConvertFrom-Json
$tag = "qt-static-$($pins.qt.version)"
$name = "qt-static-$($pins.qt.version)-$($pins.mingw.qtTool).tar.xz"
if (Test-Path (Join-Path $Prefix 'lib/cmake/Qt6/Qt6Config.cmake')) {
    Write-Host "Static Qt already present in $Prefix"
    return
}

$work = Join-Path ([IO.Path]::GetTempPath()) 'toffee-qt-download'
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
$archive = Join-Path $work $name
$sidecar = Join-Path $work "$name.sha256"

Write-Host "Downloading $name"
if ($env:TOFFEE_RELEASE_DIR) {
    Copy-Item (Join-Path $env:TOFFEE_RELEASE_DIR $name) $archive
    Copy-Item (Join-Path $env:TOFFEE_RELEASE_DIR "$name.sha256") $sidecar
} elseif ($env:GH_TOKEN -and (Get-Command gh -ErrorAction SilentlyContinue)) {
    # With a token
    gh release download $tag --repo $Repository --dir $work --pattern $name --pattern "$name.sha256"
    if ($LASTEXITCODE -ne 0) { throw "Could not download $name from the $tag release." }
} else {
    $base = "https://github.com/$Repository/releases/download/$tag"
    Invoke-WebRequest "$base/$name" -OutFile $archive
    Invoke-WebRequest "$base/$name.sha256" -OutFile $sidecar
}

$pinned = Join-Path $PSScriptRoot 'qt-static.sha256'
$pinnedText = if (Test-Path $pinned) { (Get-Content $pinned -Raw) } else { '' }
$expected = if ($pinnedText -and $pinnedText.Trim()) { ($pinnedText.Trim() -split '\s+')[0].ToLower() } else { '' }
$source = 'tools/qt-static.sha256'
if (-not $expected) {
    $source = 'the release'
    $expected = ((Get-Content $sidecar -Raw) -split '\s+')[0].Trim().ToLower()
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLower() -ne $expected) {
    throw "$name does not match the SHA-256 from $source."
}

$parent = Split-Path -Parent $Prefix
New-Item -ItemType Directory -Force $parent | Out-Null
Write-Host "Extracting to $Prefix"
$timer = [Diagnostics.Stopwatch]::StartNew()
Expand-TarXz $archive $parent
Write-Host "Extracted in $([int]$timer.Elapsed.TotalSeconds) s"
Remove-Item -Recurse -Force $work
if (-not (Test-Path (Join-Path $Prefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw "Static Qt was not extracted to $Prefix. Check that the archive folder is named $(Split-Path -Leaf $Prefix)."
}
Write-Host "Static Qt ready in $Prefix"

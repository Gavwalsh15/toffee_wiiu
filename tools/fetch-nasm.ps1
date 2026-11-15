# nasm.us publishes no checksums, so the SHA-256 below pins the archive this was tested with.
$ErrorActionPreference = 'Stop'
$version = '2.16.03'
$sha256 = '3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6'
$url = "https://www.nasm.us/pub/nasm/releasebuilds/$version/win64/nasm-$version-win64.zip"

$tools = Join-Path (Split-Path -Parent $PSScriptRoot) '.tools\nasm'
if (Test-Path (Join-Path $tools "nasm-$version\nasm.exe")) { return }

New-Item -ItemType Directory -Force $tools | Out-Null
$archive = Join-Path $tools "nasm-$version-win64.zip"
Write-Host "Downloading NASM $version"
Invoke-WebRequest $url -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLower() -ne $sha256) {
    Remove-Item $archive
    throw "nasm-$version-win64.zip does not match the expected SHA-256."
}
Expand-Archive $archive -DestinationPath $tools -Force
Remove-Item $archive

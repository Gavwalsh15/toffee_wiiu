param(
    [switch]$NoBuild,
    [string]$ToolsPath = (Join-Path $PSScriptRoot '..\.tools\idf-tools')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$firmwareBuild = Join-Path $root 'build/firmware'
$bundle = Join-Path $root 'build/firmware-app'
New-Item -ItemType Directory -Path $bundle -Force | Out-Null

function Build-Firmware {
    $idf = Join-Path $root 'third_party\esp-idf'
    if (-not (Test-Path (Join-Path $idf 'tools\idf.py'))) {
        Write-Host 'Fetching the ESP-IDF submodule (large)'
        git -C $root submodule update --init --recursive --depth 1 --jobs 8 third_party/esp-idf
        if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the ESP-IDF submodule. Clone with --recurse-submodules.' }
    }

    New-Item -ItemType Directory -Force $ToolsPath | Out-Null
    $env:IDF_TOOLS_PATH = (Resolve-Path $ToolsPath).Path
    $env:IDF_PATH = $idf
    $python = Get-ChildItem (Join-Path $env:IDF_TOOLS_PATH 'python_env\*\Scripts\python.exe') -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $python) {
        Write-Host 'Installing the ESP-IDF toolchain for the ESP32-C5 (large download)'
        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $idf 'install.ps1') esp32c5
        if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF tool install failed.' }
        $python = Get-ChildItem (Join-Path $env:IDF_TOOLS_PATH 'python_env\*\Scripts\python.exe') | Select-Object -First 1
    }
    $python = $python.FullName
    $env:IDF_PYTHON_ENV_PATH = Split-Path -Parent (Split-Path -Parent $python)
    $toolBins = Get-ChildItem (Join-Path $env:IDF_TOOLS_PATH 'tools') -Recurse -Filter '*.exe' -File | ForEach-Object DirectoryName | Sort-Object -Unique
    $env:PATH = ((@((Split-Path -Parent $python), (Join-Path $idf 'tools')) + $toolBins) -join ';') + ';' + $env:PATH

    $configRoot = Join-Path $root 'build\firmware-config'
    $sdkconfig = Join-Path $configRoot 'release.sdkconfig'
    New-Item -ItemType Directory -Path $configRoot -Force | Out-Null
    Remove-Item -LiteralPath $sdkconfig -ErrorAction SilentlyContinue
    & $python (Join-Path $idf 'tools\idf.py') -C (Join-Path $root 'firmware') -B $firmwareBuild `
        -D "SDKCONFIG=$sdkconfig" -D "SDKCONFIG_DEFAULTS=$(Join-Path $root 'firmware\sdkconfig.defaults')" reconfigure build
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
}

function Write-Images {
    $sdkArguments = Get-Content -LiteralPath (Join-Path $firmwareBuild 'flasher_args.json') -Raw | ConvertFrom-Json
    $manifest = [ordered]@{
        chip = $sdkArguments.extra_esptool_args.chip
        baud = 460800
        before = $sdkArguments.extra_esptool_args.before
        after = $sdkArguments.extra_esptool_args.after
        flashMode = $sdkArguments.flash_settings.flash_mode
        flashSize = $sdkArguments.flash_settings.flash_size
        flashFreq = $sdkArguments.flash_settings.flash_freq
        files = @()
    }
    foreach ($offset in ($sdkArguments.flash_files.PSObject.Properties.Name | Sort-Object { [Convert]::ToInt32($_.Substring(2), 16) })) {
        $source = Join-Path $firmwareBuild $sdkArguments.flash_files.$offset
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Firmware image missing from ESP-IDF build: $source"
        }
        $fileName = switch -Regex ([IO.Path]::GetFileName($source)) {
            '^bootloader\.bin$' { 'bootloader.bin'; break }
            '^partition-table\.bin$' { 'partition-table.bin'; break }
            default { 'firmware.bin' }
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $bundle $fileName) -Force
        $manifest.files += [ordered]@{
            offset = $offset
            file = $fileName
            size = (Get-Item -LiteralPath $source).Length
        }
    }
    if ($manifest.files.Count -ne 3) {
        throw "Expected bootloader, partition table, and app images; got $($manifest.files.Count)."
    }
    [IO.File]::WriteAllText((Join-Path $bundle 'manifest.json'), ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}

# Espressif's own single-file build of esptool, pinned by SHA-256.
function Write-Flasher {
    $version = '4.12.0'
    $sha256 = '42fddc5e6a05716868ad77fb43acbf53be041f97abed87ff850df1dc88140889'
    $cache = Join-Path $root 'build/esptool'
    $zip = Join-Path $cache "esptool-v$version-windows-amd64.zip"
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    if (-not (Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $sha256) {
        Write-Host "Downloading esptool $version"
        Invoke-WebRequest -UseBasicParsing -OutFile $zip `
            "https://github.com/espressif/esptool/releases/download/v$version/esptool-v$version-windows-amd64.zip"
        if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $sha256) {
            Remove-Item -LiteralPath $zip
            throw "The esptool $version download does not match its pinned SHA-256."
        }
    }
    $unpacked = Join-Path $cache 'unpacked'
    Remove-Item -LiteralPath $unpacked -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
    Copy-Item -LiteralPath (Join-Path $unpacked 'esptool-windows-amd64/esptool.exe') -Destination $bundle -Force
    Copy-Item -LiteralPath (Join-Path $unpacked 'esptool-windows-amd64/LICENSE') `
        -Destination (Join-Path $bundle 'esptool-LICENSE.txt') -Force
}

if (-not $NoBuild) {
    Build-Firmware
}
Write-Images
Write-Flasher
Write-Output "App firmware bundle ready: $bundle"
exit 0

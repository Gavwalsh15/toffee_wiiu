#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf="$root/third_party/esp-idf"

if [ ! -f "$idf/tools/idf.py" ]; then
    git -C "$root" submodule update --init --recursive --depth 1 --jobs 8 third_party/esp-idf
fi

export IDF_PATH="$idf"
. "$idf/export.sh" > /dev/null

mkdir -p "$root/build/firmware-config"
rm -f "$root/build/firmware-config/release.sdkconfig"
python "$idf/tools/idf.py" -C "$root/firmware" -B "$root/build/firmware" \
    -D "SDKCONFIG=$root/build/firmware-config/release.sdkconfig" \
    -D "SDKCONFIG_DEFAULTS=$root/firmware/sdkconfig.defaults" reconfigure build

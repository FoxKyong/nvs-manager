#!/bin/sh
# Builds the desktop NVS image tool from the same ESP-IDF nvs_flash sources and
# host shim as the native UI build. Output: .pio/tools/nvs-image (or $1).
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.pio/tools/nvs-image"}
mkdir -p "$(dirname "$out")"

c++ -std=c++17 -O1 -DESP_PLATFORM -DLINUX_TARGET \
    -I"$root/lib/esp-idf-nvs/include" -I"$root/lib/esp-idf-nvs/src" -I"$root/lib/idf-host-shim/include" \
    "$root"/lib/esp-idf-nvs/src/*.cpp "$root"/lib/idf-host-shim/src/*.cpp \
    "$root/tools/nvs-image/nvs_image.cpp" -o "$out"

echo "$out"

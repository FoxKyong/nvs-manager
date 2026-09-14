#!/usr/bin/env bash
# Builds the release files for the Cardputer target into dist/nvs-manager-<version>/.
#
#   tools/make-release.sh [--allow-dev]
#
# The version comes from NVSM_VERSION in platformio.ini; a "-dev" version is
# refused unless --allow-dev is given. Needs PlatformIO (pio) and esptool on
# PATH; set PLATFORMIO_CORE_DIR if the PlatformIO core is not in ~/.platformio.
#
# Files:
#   nvs-manager-<v>-cardputer-launcher.bin         app image, for Launcher
#   nvs-manager-<v>-cardputer-standalone.zip       bootloader, partition table,
#                                                  boot_app0 and app, flashed in
#                                                  parts; NVS stays untouched
#   nvs-manager-<v>-cardputer-full-ERASES-NVS.bin  one image from 0x0; writing it
#                                                  erases the NVS partition
#   SHA256SUMS
set -euo pipefail

die() {
    echo "${0##*/}: $*" >&2
    exit 1
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

version="$(sed -n "s/.*-DNVSM_VERSION='\"\([^\"]*\)\"'.*/\1/p" platformio.ini)"
[[ -n "$version" ]] || die "NVSM_VERSION not found in platformio.ini"
if [[ "$version" == *-dev* && "${1:-}" != "--allow-dev" ]]; then
    die "version $version is a development version; set the release version in platformio.ini, or pass --allow-dev"
fi
command -v pio >/dev/null || die "pio not found"
command -v esptool >/dev/null || die "esptool not found"
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"

pio run -e cardputer -t clean >/dev/null
pio run -e cardputer

build=".pio/build/cardputer"
boot_app0="$PLATFORMIO_CORE_DIR/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
for f in "$build/bootloader.bin" "$build/partitions.bin" "$build/firmware.bin" "$boot_app0"; do
    [[ -f "$f" ]] || die "missing $f"
done

name="nvs-manager-$version-cardputer"
out="dist/nvs-manager-$version"
rm -rf "$out"
mkdir -p "$out/$name-standalone"

cp "$build/firmware.bin" "$out/$name-launcher.bin"

cp "$build/bootloader.bin" "$build/partitions.bin" "$boot_app0" "$build/firmware.bin" "$out/$name-standalone/"
cat >"$out/$name-standalone/FLASHING.txt" <<EOF
NVS Manager $version for M5Stack Cardputer v1.0, v1.1 and ADV
Standalone install, flashed in parts

This replaces the firmware on the device (Launcher included) but leaves the
NVS partition (0x9000-0xDFFF) alone, so the data you want to inspect stays
where it is:

  esptool --chip esp32s3 --port PORT write-flash \\
      0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin

PORT is the device's serial port, e.g. /dev/ttyACM0 or COM5. The partition
table keeps NVS at 0x9000 with 0x5000 bytes, like Launcher's layout.

To get Launcher back afterwards, install it again with its own flasher.
EOF
(cd "$out" && zip -q -r "$name-standalone.zip" "$name-standalone")
rm -r "${out:?}/$name-standalone"

full="$out/$name-full-ERASES-NVS.bin"
esptool --chip esp32s3 merge-bin -o "$full" \
    0x0 "$build/bootloader.bin" 0x8000 "$build/partitions.bin" \
    0xe000 "$boot_app0" 0x10000 "$build/firmware.bin" >/dev/null

# The merged image must hold every part at its address, and its NVS range is
# 0xFF: that is what erases NVS when it is written from 0x0.
python3 - "$full" "$build/bootloader.bin" "$build/partitions.bin" "$boot_app0" "$build/firmware.bin" <<'PY'
import sys
image, parts = open(sys.argv[1], "rb").read(), sys.argv[2:]
for offset, path in zip((0x0, 0x8000, 0xE000, 0x10000), parts):
    data = open(path, "rb").read()
    if image[offset:offset + len(data)] != data:
        sys.exit(f"merged image: {path} is not at {offset:#x}")
if image[0x9000:0xE000] != b"\xff" * 0x5000:
    sys.exit("merged image: NVS range is not blank")
print(f"merged image checked: {len(image)} B, NVS range blank")
PY

(cd "$out" && sha256sum -- *.bin *.zip >SHA256SUMS)
echo "release files in $out:"
ls -l "$out"

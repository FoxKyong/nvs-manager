# Third-party software

NVS Manager's own code is under the [MIT licence](LICENSE). The firmware also contains, and the repository vendors, code from the projects below under their own licences. Read each project's licence file for its full terms.

## In the device firmware

| Component | Version | Licence | Source |
|---|---|---|---|
| Arduino core for ESP32 (arduino-esp32) | 3.3.11 | LGPL-2.1-or-later | <https://github.com/espressif/arduino-esp32> |
| ESP-IDF, as bundled with the Arduino core | 5.5.5 | Apache-2.0; bundled third-party components under their own licences | <https://github.com/espressif/esp-idf> |
| M5GFX | 0.2.28 | MIT | <https://github.com/m5stack/M5GFX> |
| M5Unified | 0.2.21 | MIT | <https://github.com/m5stack/M5Unified> |
| M5Cardputer | 1.1.1 | MIT | <https://github.com/m5stack/M5Cardputer> |
| SdFat | 2.3.1 | MIT | <https://github.com/greiman/SdFat> |

The release binaries are built from this repository with PlatformIO, and every component above is fetched at the version pinned in `platformio.ini`. To rebuild the firmware against a modified copy of a library, including the LGPL-licensed Arduino core, point PlatformIO at that copy and build again (see "Building" in the README).

## In the repository only

| Component | Where | Licence |
|---|---|---|
| ESP-IDF `nvs_flash` component, v5.5.5, unmodified | `lib/esp-idf-nvs/` | Apache-2.0 (see `lib/esp-idf-nvs/README.md`) |

It is compiled only into the desktop build (`pio run -e native`), which runs the real NVS code over a partition dump. The device firmware uses the copy that ships with ESP-IDF.

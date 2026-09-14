# esp-idf-nvs (vendored)

ESP-IDF's `nvs_flash` component, used **only by the desktop (`native`) build** so the UI can run against the real NVS library and a raw partition dump. The Cardputer firmware uses the copy that ships with the Arduino/ESP-IDF framework instead.

| | |
|---|---|
| Origin | https://github.com/espressif/esp-idf, `components/nvs_flash` |
| Tag | `v5.5.5` (commit `ff1bac0aeecdd2b797b9c3a558c6bd03629bc013`) — the ESP-IDF release behind the Cardputer build's pioarduino platform `55.03.311` |
| License | Apache-2.0, see [LICENSE](LICENSE). Most files carry their own SPDX or Apache header; the rest fall under the repository licence of ESP-IDF. |

## What was taken

- `include/`: `nvs.h`, `nvs_flash.h`, `nvs_handle.hpp`, unchanged.
- `src/`: the sources ESP-IDF itself builds for its `linux` target, unchanged. The two headers from `private_include/` (`nvs_constants.h`, `nvs_internal.h`) were moved into `src/`, also unchanged.

**Left out:** the encrypted-partition code (ESP-IDF does not support it on Linux either) and the bootloader variant.

## How it is built

`library.json` restricts the library to the `native` platform and defines the switches ESP-IDF's build system sets for its Linux target:

- `ESP_PLATFORM` — enables `nvs_flash_init_partition()` and leaves out host-only debug checks in `nvs_storage.cpp`;
- `LINUX_TARGET` — leaves out the encrypted-partition code.

The missing ESP-IDF headers (`esp_err.h`, `esp_partition.h`, …) come from the project's own `lib/idf-host-shim`. The shim also defines `lookup_nvs_encrypted_partition`: it is declared for every target but only defined next to the left-out encryption code, and it returns `ESP_ERR_NVS_ENCR_NOT_SUPPORTED`.

The Cardputer environment lists both libraries in `lib_ignore`, so they never shadow the framework's own `nvs_flash`.

## Updating

Replace the files from a newer tag and update the table above. Keep the tag in step with the ESP-IDF release used by the Cardputer build, so both targets run the same NVS code.

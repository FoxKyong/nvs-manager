# Research notes — phase 1

Date: 2026-09-13. Scope: what the firmware can rely on before any code is written.

Every statement is tagged:

- **verified** — read in the source listed in [Sources](#1-sources) or measured locally,
- **inference** — derived from the sources, not tested,
- **open** — needs a build or real hardware to settle.

## 1. Sources

| Source | Version inspected |
|---|---|
| [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) | `4d7e73c` (2026-09-11), plus wiki pages *Explaining the project* and *Obtaining binaries to launch* |
| [m5stack/M5Cardputer](https://github.com/m5stack/M5Cardputer) | 1.1.1, `f139285` (2026-07-21) |
| [m5stack/M5Unified](https://github.com/m5stack/M5Unified) | 0.2.21, `8530f53` (2026-08-27) |
| [espressif/esp-idf](https://github.com/espressif/esp-idf) `components/nvs_flash` | branch `release/v5.5`, fetched 2026-09-13 |
| [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32) `cores/esp32` | tag `3.3.11` |
| [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) | releases `55.03.39`, `55.03.311` |
| [lexilexiko/0N3P0rK](https://github.com/lexilexiko/0N3P0rK) | `dde941e` (2026-09-12) — reference firmware for both Cardputers |

## 2. NVS behaviour (ESP-IDF v5.5)

**Page geometry — verified** (`private_include/nvs_constants.h`): a page is one flash sector (4096 B) holding **126 entries of 32 B**. A 20 KiB partition has 5 pages, so `total_entries = 630`.

**`nvs_get_stats()` — verified** (`PageManager::fillStats`, `Page::calcEntries`):

- ACTIVE and FULL pages add `used += usedEntryCount` and `free += 126 − usedEntryCount`. The source comments this as *"equivalent free + erase entries"*: **erased entries count as free immediately**, without waiting for garbage collection.
- Uninitialised and corrupt pages, and pages on the free list, count as fully free.
- `available_entries = free_entries − 126` when `free_entries ≥ 126`, otherwise `0`. One page is always held back for garbage collection.

Consequences:

- The failure state from the brief (4 full pages and 1 empty page) gives `free = 126` and `available = 0`. The dashboard has to show `available_entries`: `free_entries` looks healthy in exactly this state. **verified** (arithmetic on the source)
- Deleting a namespace should raise `available_entries` right away, in the next `nvs_get_stats()` call. **inference** — confirm in acceptance test C.

**Per-namespace usage — verified** (`include/nvs.h`): `nvs_get_used_entry_count()` does *not* include the namespace entry itself; the documentation says to add one.

**Entry span — verified** (`Page::writeItem`): a string or blob chunk takes one header entry plus `ceil(size / 32)` data entries. One chunk holds at most `32 × 125 = 4000 B`. Larger blobs are split into `BLOB_DATA` chunks plus a `BLOB_IDX` entry. How the entry iterator reports chunked blobs is **open** — check in phase 3.

**Empty and orphaned namespaces — verified** on the test device's real NVS:

- `nvs_stats_t.namespace_count` counts every namespace entry. Namespaces that no longer hold any key (three on the test device) are invisible to the entry iterator, but each still takes one entry.
- **Orphaned entries** occur too: 18 entries of one key whose namespace entry no longer exists (namespace index 2; `nvs_tool.py -i` reports *"Undefined namespace index"*). They occupy space but have no name, so the public API can neither list nor delete them. Only erasing the partition (or rewriting it raw) reclaims them.
- The reader therefore reports how many used entries it could not attribute: 21 of 248 on the test device (3 empty namespaces + 18 orphaned).
- **A new namespace adopts orphaned entries — verified** (desktop build of ESP-IDF v5.5.5 nvs_flash on the test device's dump). The orphan is one blob key `eeprom` with namespace index 2. Creating any new namespace assigns the lowest free index, which is 2, and the orphan becomes a key of the new namespace. `nvsm_test` then held 6 keys although 5 were written. **Consequences:**
  - after an install, a firmware can find stale keys from an unrelated, long-deleted namespace;
  - deleting such a namespace also erases the adopted data. NVS Manager counts and lists whatever the namespace holds at that moment, so the confirmation shows the real key count.

**Iterator bug — verified** (`nvs_storage.cpp`, `Storage::fillEntryInfo`): the namespace name is copied only when the entry's namespace index is found. For an orphaned entry, the iterator keeps the *previous* entry's name, so the orphan is reported under an unrelated namespace. On the device this showed as 48 keys for `nvs.net80211`, which really has 47. **Consequence:** count keys by iterating inside one namespace (filtered by index), never by grouping a partition-wide iteration by `namespace_name`.

**Chunked blobs — verified** (`Storage::nextEntry`): the iterator skips `BLOB_IDX` and all but the first data chunk, so a blob shows up once with type `NVS_TYPE_BLOB`; `nvs_get_blob(…, NULL, &len)` returns its full size.

**Initialisation can write — verified** (`PageManager::load`): unless the partition is flagged read-only, loading repairs the effects of a power loss. It erases a duplicate of the last written item and finishes an interrupted page move. Every ESP-IDF firmware does this at boot; it is not something NVS Manager can opt out of while keeping NVS writable. The test device's NVS stayed bit-identical across repeated boots of NVS Manager, so no repair was pending there.

**Host-side tool — verified** (`nvs_flash/nvs_partition_tool/nvs_tool.py`): ESP-IDF ships a parser for raw NVS dumps, with JSON output and an integrity check. Use it in tests to validate raw backups made by the firmware, off the device.

## 3. Critical: the Arduino core erases NVS before `setup()`

**Verified** in arduino-esp32 `3.3.11`, `cores/esp32/esp32-hal-misc.c`, `initArduino()`:

```c
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    if (partition != NULL) {
        err = esp_partition_erase_range(partition, 0, partition->size);
        ...
```

`initArduino()` is called from the core's `app_main` before `setup()`, and it is not a weak symbol. Only `init()` and `initVariant()` are weak, and they run after the erase.

**Impact.** A plain Arduino build of NVS Manager would wipe exactly the state it is meant to diagnose, before any of its own code runs. That breaks the core rule *never erase automatically*. The same code runs in every Arduino-esp32 3.x firmware a launcher starts, so one such firmware can silently wipe the shared NVS for all others. **inference** for other firmware; 2.x cores not checked. This is worth explaining on the help screen.

**Options.**

| | Approach | Cost | Status |
|---|---|---|---|
| A | Keep Arduino. Link with `-Wl,--wrap=nvs_flash_init` so the core never sees the two triggering error codes; our code calls `__real_nvs_flash_init` and reports the true result. Also wrap `esp_partition_erase_range` to refuse NVS partitions unless our own code explicitly armed the call. | small | **open** — works only if the core's call site goes through the linker wrap |
| B | ESP-IDF framework without Arduino. Full control over NVS initialisation. | own keyboard and SD glue; the M5Cardputer keyboard classes may depend on Arduino (**open**) | fallback |
| C | Arduino as an ESP-IDF component with autostart disabled | would still need most of `initArduino()` re-implemented | not pursued |

**Proposal:** try A in phase 2. Prove it with a destructive hardware test on the dedicated test device: flash a crafted NVS image that makes `nvs_flash_init()` return `ESP_ERR_NVS_NO_FREE_PAGES`, boot, and confirm that the data survived. Fall back to B if A cannot be proven.

**Phase 2 status.** A is implemented in `src/nvs/NvsBootGuard.cpp`. **verified statically:** in the linked `firmware.elf`, `initArduino` calls `__wrap_nvs_flash_init` (twice) and `__wrap_esp_partition_erase_range` (`xtensa-esp32s3-elf-objdump --disassemble=initArduino`). The wrap therefore reaches the core's call site. The erase guard refuses only an erase of the whole NVS partition, so sector erases by NVS garbage collection keep working.

**Runtime proof — verified on the test Cardputer v1.0 (phase 3):**

The test image was a real 20 KiB NVS dump whose one empty page was rewritten as a FULL page (valid header CRC, no entries, lowest sequence number). `nvs_tool.py` reports all page CRCs OK and *"No free (empty) page found"*, which is the `ESP_ERR_NVS_NO_FREE_PAGES` condition of `PageManager::load`.

| Firmware | Boot log | NVS after boot |
|---|---|---|
| with guard | core: `initArduino(): Failed to initialize NVS! Error: -1`; guard report: `ESP_ERR_NVS_NO_FREE_PAGES` | **bit-identical** to the written image (SHA-256 `b985ad79…`) |
| control build without the wraps | no error at all | **all five pages erased** (`0xFF`); the reader then finds `used=0 namespaces=0` |

With the guard, the core received `ESP_FAIL` and never attempted the erase, so the erase wrap did not have to act. Without it, the core wiped the partition silently and re-initialised it. This is what any Arduino-esp32 3.x firmware does to a shared NVS in this state.

**Test hygiene:** in a first control run, a second reset (esptool's automatic reset followed by the capture script's own RTS pulse) interrupted the erase. The result was a partly erased NVS: three namespaces gone, most other data intact. It looked like a different failure mode. Hardware tests must write with `--after no-reset` and boot exactly once.

## 4. Launcher

**Canonical repository — inference:** `bmorcelli/Launcher`. The older `bmorcelli/M5Stick-Launcher` URLs redirect to it.

**Build — verified:** PlatformIO + Arduino on pioarduino `55.03.39` (Arduino 3.3.9 / ESP-IDF 5.5.4), with a custom prebuilt Arduino library bundle.

**One binary for both Cardputers — verified** (`boards/m5stack-cardputer/`): a single env `m5stack-cardputer` (`DEVICE_NAME "M5Stack Cardputer & ADV"`) probes a TCA8418 keyboard controller at I²C `0x34` on SDA 8 / SCL 9. If the probe fails, it uses the GPIO matrix keyboard. `CardputerADV.md` in that folder still describes an older, separate ADV env that no longer exists.

**Flash layout of a Cardputer running Launcher — verified** (`support_files/custom_8Mb.csv`):

| Name | Type / subtype | Offset | Size |
|---|---|---|---|
| nvs | data / nvs | `0x9000` | `0x5000` |
| otadata | data / ota | `0xE000` | `0x2000` |
| app0 | app / **test** (Launcher itself) | `0x10000` | `0x150000` |
| coredump | data / coredump | `0x160000` | `0x10000` |

Installed apps get OTA partitions that Launcher's partition manager creates at runtime. Launcher's changelog records that it *removed `phy_init` and increased NVS to `0x5000`*.

**NVS written by Launcher — verified** (grep over `src/`):

| Namespace | Purpose |
|---|---|
| `launcher` | settings |
| `l_wifi` | Wi-Fi credentials (stored encrypted) |
| `l_apps` | installed app registry |
| `l_bonds` | per-firmware stash of NimBLE bonds |
| `nimble_bond` | live NimBLE bond store; Launcher swaps it with `l_bonds` on firmware switch |
| `uiflow` | UIFlow2 data |
| `touch_cal` | touchscreen calibration (touch devices) |

The brief lists `launcher`, `l_wifi`, `l_apps`, `l_bonds` and `uiflow` as LAUNCHER class. **Proposal:** add `nimble_bond` and `touch_cal`. Deleting `nimble_bond` or `l_bonds` can break BLE pairings of *other* firmware.

**Launcher also changes NVS on its own — verified** (wiki): every firmware install resets the `nvs.net80211` namespace to avoid NVS exhaustion. Its WebUI already offers an NVS browser and editor. NVS Manager differs by running on the device itself, without a network, and by adding capacity diagnostics, the protection model and backups.

**Accepted image formats — verified** (wiki, `sd_functions.cpp: updateFromSD`):

- an **app-only** image (ESP image magic `0xE9` at offset 0) goes into an OTA partition;
- a **merged** image with a partition table at `0x8000` (magic `AA 50 01`): Launcher reads the table and extracts the app partition (factory / ota / test subtypes) plus SPIFFS/LittleFS data.

The merged image's own NVS, otadata and bootloader are not written. **inference** from the code path — confirm on hardware.

**Install over the serial console — verified** (Launcher 2.9.1 on the test Cardputer v1.0). The console command `flash firmware <name> <size>` installs an app-only image with no SD card and no network. Launcher answers `READY`, acknowledges 2048-byte chunks with `ACK`, and reboots into the app. `tools/runner/launcher_install.py` automates it.

- **Partitions.** Launcher added `nvsman` (ota_0, `0x190000`, `0x90000`) and pointed otadata at it. The existing partitions stayed where they were. A second install under the same name would add another partition (`nvsma1`), unless the first is removed with `partition delete <label>`. A reinstall that way reused the label and the offset. Its only NVS change was Launcher's usual `l_wifi` rewrite.
- **NVS**, compared entry by entry with `tools/nvs-diff.py`:
  - every Launcher boot rewrote the three `l_wifi` keys at the same size, and nothing else;
  - `l_wifi` follows the SD card. A boot with a different card, with no `config.conf` on it, erased those keys without writing new ones (Launcher logged `config.conf not found, creating with defaults`). A later boot with the original card wrote them back. That Launcher mirrors the card's `config.conf` into `l_wifi` is an **inference** from this pair of dumps, not checked in its code;
  - the install added the namespaces `l_apps` (`nvsman`, `n_nvsman`) and `l_bonds` (`owner`), and rewrote `launcher/last_app`;
  - no other namespace changed. In particular `nvs.net80211` was not reset, although the wiki says an install resets it. That reset may belong to other install paths; not checked.
- **NVS Manager under Launcher.** It booted, read the shared NVS (17 namespaces), mounted the SD card, had the UI ready after 530 ms, and blocked no erases.

**Boot flow — partly verified.**

- **The bootloader decides.** Launcher ships its own bootloader. It logs `Turned on because (1= POWERON_RESET) (Other= Probably forced by launcher)`.
- **Other resets start the app — verified.** After a USB reset (reason 21), the installed app started directly, with no Launcher start screen. This also happened after otadata was erased: the bootloader then picked the first OTA app, not Launcher's `test` partition.
- **Power-on starts Launcher — verified.** With the battery switched off, unplugging the USB cable and plugging it back in brought up Launcher's start screen (seen by the owner). The serial console was reachable during that screen.
- **A restart from the app starts the app again — verified.** Exit in NVS Manager restarts in software (`RTC_SW_CPU_RST`, bootloader reason 12), and NVS Manager came back with NVS unchanged. Getting back to Launcher takes a real power-off. A device on USB stays powered with its switch off, so the cable has to come out too.
- **USB power cycle from the runner — did not work.** `uhubctl` on the runner's port dropped and re-enumerated the device, but it did not boot, even with the battery switched off. That port probably does not really switch VBUS (**inference**). Replugging the cable by hand works.

**Serial monitoring without a reset — verified** (pyserial 3.5 on Linux, ESP32-S3 USB Serial/JTAG):

- **Why a plain open resets.** Opening the port raises DTR and RTS, and pyserial then applies DTR before RTS. DTR low while RTS is high is the chip's reset condition.
- **Opening without a reset.** Set `dtr = True` and `rts = False` before `open()`, and the device keeps running.
- **Resetting on purpose.** Pulse RTS while DTR is low.

## 5. Release images and the NVS region

**Verified locally:** `esptool merge-bin` fills gaps with `0xFF`. In a test merge of `0x0` / `0x8000` / `0x10000`, the whole range `0x9000–0xFFFF` was `0xFF`. Writing a merged image at `0x0` therefore **erases the NVS** and also overwrites whatever lives at `0x10000`. On a Launcher device that is Launcher itself.

**Proposal for release artefacts:**

1. **App-only image**: for Launcher, and for updating a standalone install at `0x10000`.
2. **Separate parts** (bootloader `0x0`, partition table `0x8000`, `boot_app0` `0xE000`, app `0x10000`): the primary standalone install. It never touches `0x9000–0xDFFF`, so the NVS under diagnosis survives.
3. **Merged full image**: convenience only, with a prominent warning that it wipes NVS.

Note: the pioarduino build writes such a merged image on its own (`.pio/build/<env>/firmware.factory.bin`: bootloader, partitions, `boot_app0` and app from `0x0`). It must not end up in a release under a neutral name. **verified** (build log)

**Standalone partition table requirement — inference:** keep `nvs` at `0x9000` / `0x5000` and `otadata` at `0xE000`, like Launcher. A different NVS offset or size would make the existing data unreadable or produce init errors, which is exactly what triggers the erase in section 3.

## 6. Devices

| | Cardputer v1.0 | Cardputer v1.1 | Cardputer ADV |
|---|---|---|---|
| Module | M5StampS3 (ESP32-S3, 8 MB flash) | M5StampS3A, larger keys, power optimisations (per [hackster.io](https://www.hackster.io/news/m5stack-revisits-the-cardputer-launches-a-new-model-with-better-keyboard-and-battery-life-15f15e0de535); not verified pin by pin) | M5StampS3A |
| M5Unified board id | `board_M5Cardputer` | `board_M5Cardputer` (**inference**) | `board_M5CardputerADV` |
| Keyboard | GPIO matrix | GPIO matrix | TCA8418 over I²C `0x34`, SDA 8 / SCL 9 |

**One binary for v1.0, v1.1 and ADV — verified on hardware.** The same image ran on a Cardputer v1.0, a Cardputer v1.1 and a Cardputer ADV. The v1.1 behaved like the v1.0:

- the GPIO matrix keyboard;
- a 60 GB card formatted FAT32, where a metadata export was written and read back;
- Exit;
- NVS unchanged by the test;
- the UI ready after 515 ms.

It ran under Launcher 2.9.1 and was restored from a full flash backup afterwards. On the ADV, the device build detected the board with the G8/G9 probe below and read the TCA8418 keyboard. It mounted the card with the GPIO5 enable and drew the same display. Owner's check on the ADV, under Launcher 2.9.1:

- navigation, search and detail screens worked;
- a metadata export was written to its FAT32 card and read back;
- Exit restarted into NVS Manager;
- NVS was unchanged by browsing, exporting and exiting.

The UI was ready 985 ms after start, against 530 ms on the v1.0.

**The ADV's own NVS was the brief's problem case:** 490 of 630 entries used, only 14 available (CRITICAL), in 34 namespaces. Installing through Launcher did not make it worse. Launcher's install also dropped the registry entries (`l_apps`) of two apps whose partitions no longer existed. The ADV was restored from a full flash backup afterwards, and the read back matched it.

**Pins — verified** (Launcher env; the SD pins also match M5Unified's table):

- display ST7789 135×240: BL 38, RST 33, DC 34, MOSI 35, SCLK 36, CS 37;
- SD over SPI: CS 12, SCK 40, MISO 39, MOSI 14;
- battery ADC: 10.

On the ADV, Launcher drives GPIO5 high to keep the SD card working (its `CardputerADV.md`).

**SD card file systems — verified:**

- The Arduino `SD` library cannot mount exFAT. The framework's FatFs (arduino-esp32 3.3.11) is built with `FF_FS_EXFAT 0`. A 32 GB card from the test Cardputer failed with `f_mount` "(13) There is no valid FAT volume".
- The device build uses SdFat 2.3.1 (`SdFs`) instead. It mounts FAT12/16/32 and exFAT. The same card mounted as exFAT with 32 KiB clusters at 25 MHz SPI. All three exports were written and read back on it. The raw backup's SHA-256 matched a flash read of the NVS partition taken afterwards, so writing to the card left NVS unchanged.
- SdFat compiles its formatter, but nothing in NVS Manager calls it. Cards are never formatted.
- SdFat leaves `File` undefined when `FS.h` is reachable, as it is through M5GFX. The code uses `FsFile`, and the build sets `DISABLE_FS_H_WARNING`.

**LilyGo T-Deck Plus, later — verified** (Launcher env `lilygo-t-deck-plus`): extends `lilygo-t-deck` — ESP32-S3, 16 MB flash, PSRAM, ST7789 240×320, GT911 touch, keyboard, `-DT_DECK_PLUS=1`.

**Libraries and NVS — verified:**

- M5Unified's IMU init *reads* namespace `M5Unified` (read-only) and writes it only from an explicit `saveOffsetToNVS()` call.
- M5Cardputer does not use NVS.
- **M5GFX writes NVS.** `M5GFX::init_impl()` (0.2.28) caches the autodetected board in namespace `M5GFX`, key `AUTODETECT`, whenever the detected board differs from the cached value or from the compile-time `M5GFX_BOARD` hint. Plain `M5.begin()` goes through this path, so the device build configures the display by hand and never calls `M5.begin()`.
- M5GFX tells the Cardputer models apart with a passive GPIO probe: the ADV has I²C pull-ups on G8/G9, while v1.x drives those pins into the keyboard decoder. The device build reuses this probe.

**Serial logging can freeze the firmware — verified** (arduino-esp32 3.3.11 `HWCDC.cpp`, and on the test Cardputer):

- **The mechanism.** With `ARDUINO_USB_CDC_ON_BOOT`, `Serial` is the chip's USB Serial/JTAG. Once a host has read the port, `HWCDC::write()` keeps treating it as connected while USB stays plugged in. When the reader goes away, every write waits up to 20 × `tx_timeout_ms` (default 100 ms) for buffer space before it gives up — and the next write waits again.
- **What it did.** The boot report (over 200 lines) stalled `setup()` for minutes after an esptool reset. The display stayed black and ignored keys. This was seen twice. The empty coredump partition (core dumps to flash are enabled) ruled out a crash or watchdog.
- **The fix.**
  - a 10 ms TX timeout and a 4 KiB TX buffer;
  - no logging for 3 s after a short write;
  - the UI is drawn before the long report;
  - the boot log states the reset reason and when the UI was ready.
- **The result.** With a reader, all report lines still arrive and the UI is ready 412 ms after start. After an unattended esptool reset, the dashboard appeared at once (checked by the owner on the device).

Any Arduino firmware that logs a lot this way can hang the same way whenever something that once read the port stops reading. **inference**, not tested beyond this firmware.

**Registry versus git — verified:** the registry release M5Cardputer 1.1.1 (2025-09-28) has a different `KeysState` than the git master inspected in section 1 (no arrow/Esc flags). Read the copy PlatformIO installs under `.pio/libdeps/`, not the upstream clone.

## 7. Toolchain (proposed)

- PlatformIO Core 6.2.0 on x86_64.
- Platform: pioarduino **`55.03.311`** (Arduino 3.3.11 / ESP-IDF 5.5.5, released 2026-07-24), pinned by exact release URL. It is the newest release and on the same ESP-IDF 5.5 line as Launcher, which avoids NVS format-version surprises. **inference**
- Libraries pinned: M5Unified 0.2.21, M5Cardputer 1.1.1.
- 0N3P0rK builds on the legacy `espressif32@6.12.0` (Arduino 2.x). It is a useful reference for board settings but not for the toolchain.

## 8. Seeing the UI without hardware

Checking every screen change on a real display is slow. **Decision:** develop the UI mainly in a PC build that draws into an SDL window, and use hardware at milestones.

| Option | What it runs | UI fidelity | Use |
|---|---|---|---|
| **Native build + SDL** (`platform = native`) | our UI and backend code compiled for Linux | same M5GFX drawing code and fonts. Keyboard comes from the PC; speed and panel colours are not the real ones | **main UI loop** |
| Espressif QEMU (`-machine esp32s3`) | the real firmware image, including bootloader, flash and NVS | only a virtual framebuffer that does not exist on real hardware; no Cardputer keyboard | later, for backend tests without hardware (e.g. section 3) |
| Wokwi | cloud simulator; its CLI needs a service token | Cardputer support not verified | not pursued |

**Verified:**

- M5GFX and M5Unified both ship a `PlatformIO_SDL` example with `platform = native`. The device and window scale are selected with `M5GFX_BOARD` and `M5GFX_SCALE`.
- Launcher develops its own UI the same way (`native/`, SDL backend, real drawing code, stubbed hardware).
- ESP-IDF QEMU for ESP32-S3 takes a merged flash image and offers only a virtual framebuffer.

**Consequence for the architecture:** the brief's separation (backend never draws, UI never calls the NVS API, a platform layer for keyboard/SD/device) is what makes the native build cheap. Keyboard input and SD access need a native implementation next to the Cardputer one.

**Real NVS data on the PC — verified (phase 3):** the native build runs ESP-IDF's own `nvs_flash` (v5.5.5, vendored unchanged in `lib/esp-idf-nvs`) over a RAM copy of a raw partition dump, selected with `NVSM_NVS_IMAGE`. The missing platform pieces come from `lib/idf-host-shim` (MIT):

- `esp_partition` with NOR-flash write semantics;
- CRC-32;
- `esp_err_to_name` and logging.

What it takes to build it:

- the switches ESP-IDF's build sets for its Linux target, `ESP_PLATFORM` and `LINUX_TARGET`;
- a stub for `lookup_nvs_encrypted_partition`, which is declared for all targets but defined only next to the encryption code;
- an explicit `lib_ignore` in the Cardputer env: `"platforms": "native"` in `library.json` did not keep the libraries out of the device build.

Fed the test Cardputer's NVS dump, the desktop build printed **the same 114 NVS report lines as the device** (stats, namespaces, every key with type and size). The only difference is the device-only boot-guard line. The crafted no-free-page image yields the same `ESP_ERR_NVS_NO_FREE_PAGES` as on the device. UI work can therefore use real dumps, including broken ones.

QEMU emulates Xtensa in software (TCG). KVM and nested virtualisation cannot accelerate it on an x86 host. **inference** (general QEMU/KVM property, not measured).

## 9. Differences from the original brief

- Generic scope: NVS Manager for ESP32 devices, not M5-only; standalone **and** Launcher images.
- The LAUNCHER namespace class gains `nimble_bond` and `touch_cal`.
- Section 3 needs a design measure the brief did not anticipate.
- The brief's warning *"check that direct upload does not overwrite NVS"* now has a concrete answer: a merged image at `0x0` does (section 5).

## 10. Open questions

1. Does installing from the SD card or the WebUI reset `nvs.net80211`, as the wiki says? The serial install did not (section 4).
2. Does the firmware build natively on aarch64 (the hardware runner)? Secondary goal. Already known: esptool 5.4.0 and pyserial 3.5 install with pip on the runner (Debian 13, aarch64).

Answered since phase 1:

- board detection and NVS use of M5GFX (section 6);
- the runtime proof of the boot guard (section 3);
- how the iterator reports chunked blobs (section 2);
- the real NVS library in the native build (section 8).

## 11. Restoring a raw backup (1.0.0)

**Design.** The brief left restore out of 0.1 as the riskiest operation. The implementation keeps every earlier safety rule:

- **Checks before writing.** The backup's manifest must exist and match the file's size and SHA-256, the partition label, offset and size, and an unencrypted NVS. Every initialised page must carry a known state and a header CRC computed the way `nvs::Page::Header::calculateCrc32` does. A backup identical to the current NVS is refused as having nothing to restore.
- **Device check.** From 1.0.0, manifests record `device_id`, the first 8 bytes of a SHA-256 over a fixed prefix and the MAC address. A mismatch blocks the restore, because NVS holds per-device PHY calibration and pairings. Manifests from 0.9.0 have no `device_id`; they only get a warning. The hash is not a secret: the MAC address space is small enough to search.
- **Undo first.** Before writing, the current NVS is saved as a new raw backup with its manifest. If that fails, nothing is written.
- **Writing.** `nvs_flash_deinit_partition`, then for each 4 KiB sector: erase, write, read back and compare, then `nvs_flash_init_partition`. The boot guard refuses only a single erase of the whole partition (section 3), so it stays active. Opening the partition afterwards may complete a page move that the backup caught in progress, as NVS does after a power loss.

**Verified in the desktop build** (ESP-IDF v5.5.5 `nvs_flash` over a synthetic image):

- backup, delete a namespace, restore: a raw backup taken afterwards is byte-identical to the one restored;
- the automatic backup holds the state just before the restore;
- a changed byte, another device's `device_id` and an identical backup are refused;
- a 0.9.0-style manifest is accepted with a warning.

**Verified on hardware** (test Cardputer v1.0, under Launcher 2.9.1, exFAT card; the owner operated the device). The steps, in order:

1. A raw backup.
2. Deleting one APP namespace, which freed one entry.
3. Restoring the backup. The automatic backup before the restore held the state after the deletion.

A flash read of the NVS afterwards matched the restored backup's SHA-256 and the state before the test. `tools/nvs-diff.py` found no changed key.

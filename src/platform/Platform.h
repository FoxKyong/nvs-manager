#pragma once

#include <cstdint>
#include <cstdio> // M5GFX's DataWrapper.hpp uses fopen() without including it
#include <string>
#include <vector>

#include <M5GFX.h>

// Everything the application needs from a device. One implementation per
// target lives under platform/<target>/; the UI and the NVS backend never
// touch pins, buses or SDL directly.
namespace platform {

enum class Key : uint8_t {
    None,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Backspace,
    Tab,
    Char, // printable ASCII in KeyEvent::ch
};

struct KeyEvent {
    Key key = Key::None;
    char ch = 0;
};

void begin();

const char *deviceName();
lgfx::LovyanGFX &display();

// Returns true and fills `out` when a key press is waiting.
bool pollKey(KeyEvent &out);

// True while `key` is held down. Only Key::Enter is tracked (for hold-to-confirm);
// kept current by pollKey(), which the main loop calls continuously.
bool isHeld(Key key);

uint32_t millis();
void delay(uint32_t ms);

// Leaves the application: restarts the device, ends the desktop program.
[[noreturn]] void restart();

// Serial log line with a "[TAG] " prefix. Never pass NVS values here.
void logf(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Target-specific boot diagnostics (e.g. the NVS boot guard report).
void logBootDiagnostics();

const char *chipModel();

// Removable storage: the SD card on the device, a directory named by
// NVSM_SD_DIR on the desktop. Paths are absolute within it, such as
// "/NVSManager/backups/nvs-0001.bin". Nothing here ever formats a card.
bool storageMount(); // (re)tries to mount; returns whether storage is usable
bool storageMounted();
// Why storage is not usable, worded for the user ("" when mounted), such as
// "no SD card" or "card is NTFS, format it as exFAT or FAT32". Set by
// storageMount(). FAT12/16/32 and exFAT cards mount.
const char *storageProblem();
bool storageMakeDirs(const std::string &dir); // like mkdir -p
bool storageExists(const std::string &path);
// Writes "<path>.tmp" and renames it, so a failed write never leaves a
// truncated file under the final name.
bool storageWrite(const std::string &path, const uint8_t *data, size_t size);
bool storageRead(const std::string &path, std::vector<uint8_t> &out);
// Names (not paths) of the files directly in `dir`, in no particular order.
// False when the directory cannot be read, for example because it does not exist.
bool storageList(const std::string &dir, std::vector<std::string> &names);

// Short identifier of this device for backup manifests, so a restore can tell
// a backup of another device. A hash of the MAC address, not the MAC itself.
std::string deviceId();

} // namespace platform

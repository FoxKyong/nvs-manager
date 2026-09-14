#include "platform/Platform.h"

#include <cstdarg>
#include <memory>

#include <Arduino.h>
#include <M5Unified.h> // m5::In_I2C, used by the ADV keyboard controller driver
#include <SPI.h>
#include <SdFat.h>

#include <cstring>
#include <string>
#include <esp_system.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <utility/Keyboard/Keyboard.h>
#include <utility/Keyboard/KeyboardReader/IOMatrix.h>
#include <utility/Keyboard/KeyboardReader/TCA8418.h>

#include "nvs/NvsBootGuard.h"

namespace platform {
namespace {

// Pin assignments come from M5GFX 0.2.28 (M5GFX::autodetect) and Launcher's
// m5stack-cardputer env; the display is wired the same on Cardputer v1.x and ADV.
//
// The display is configured by hand on purpose: M5GFX's autodetect (and so
// M5.begin()) caches the detected board in NVS, and this application must not
// write NVS on its own.
class CardputerDisplay : public lgfx::LGFX_Device {
public:
    CardputerDisplay() {
        auto bus = busDevice_.config();
        bus.spi_host = SPI3_HOST;
        bus.spi_mode = 0;
        bus.freq_write = 40000000;
        bus.freq_read = 16000000;
        bus.spi_3wire = true;
        bus.pin_sclk = 36;
        bus.pin_mosi = 35;
        bus.pin_miso = -1;
        bus.pin_dc = 34;
        busDevice_.config(bus);
        panelDevice_.setBus(&busDevice_);

        auto panel = panelDevice_.config();
        panel.pin_cs = 37;
        panel.pin_rst = 33;
        panel.panel_width = 135;
        panel.panel_height = 240;
        panel.offset_x = 52;
        panel.offset_y = 40;
        panel.readable = true;
        panel.invert = true;
        panelDevice_.config(panel);

        auto light = lightDevice_.config();
        light.pin_bl = 38;
        light.freq = 256;
        light.pwm_channel = 7;
        light.offset = 16;
        lightDevice_.config(light);
        panelDevice_.setLight(&lightDevice_);

        setPanel(&panelDevice_);
    }

private:
    // Named apart from LGFXBase's own protected _panel.
    lgfx::Bus_SPI busDevice_;
    lgfx::Panel_ST7789 panelDevice_;
    lgfx::Light_PWM lightDevice_;
};

constexpr int kPinSysSda = 8; // ADV: internal I2C; v1.x: 74HC138 input
constexpr int kPinSysScl = 9;

// SD card on its own SPI bus (FSPI), apart from the display on SPI3. Pins as in
// M5Cardputer's sdcard example and Launcher.
constexpr int kPinSdCs = 12;
constexpr int kPinSdSck = 40;
constexpr int kPinSdMiso = 39;
constexpr int kPinSdMosi = 14;
constexpr int kPinAdvSdEnable = 5; // Launcher drives it high on the ADV for the card to work
constexpr uint32_t kSdFastHz = 25000000;
constexpr uint32_t kSdSlowHz = 4000000;

// SdFat rather than the Arduino SD library: the framework's FatFs is built
// without exFAT (FF_FS_EXFAT 0), and most cards over 32 GB ship as exFAT.
// SdFs mounts FAT12/16/32 and exFAT; it has no code path that formats.
SdFs g_sd;
bool g_sdMounted = false;
uint32_t g_sdHz = 0;
std::string g_sdProblem = "not checked yet";

uint32_t le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// The card answers but holds no volume SdFat can mount: say what is there
// instead. Reads only; nothing is written to the card.
std::string describeUnmountable() {
    uint8_t s[512];
    SdCard *card = g_sd.card();
    if (card == nullptr || !card->readSector(0, s)) return "SD card cannot be read";
    if (s[510] != 0x55 || s[511] != 0xAA) return "card is not formatted";

    // Sector 0 is either a volume boot record ("superfloppy") or an MBR.
    bool bootRecord = s[0] == 0xEB || s[0] == 0xE9;
    if (!bootRecord) {
        const uint8_t type = s[0x1BE + 4];
        if (type == 0xEE) return "card uses GPT partitioning, format it as exFAT or FAT32";
        if (type == 0x00) return "card has no partition, format it as exFAT or FAT32";
        const uint32_t first = le32(s + 0x1BE + 8);
        bootRecord = first != 0 && card->readSector(first, s);
    }
    if (bootRecord && std::memcmp(s + 3, "NTFS    ", 8) == 0) return "card is NTFS, format it as exFAT or FAT32";
    return "card has no FAT or exFAT volume, format it";
}

const char *volumeName(uint8_t fatType) {
    switch (fatType) {
    case FAT_TYPE_FAT12: return "FAT12";
    case FAT_TYPE_FAT16: return "FAT16";
    case FAT_TYPE_FAT32: return "FAT32";
    case FAT_TYPE_EXFAT: return "exFAT";
    default: return "unknown";
    }
}

// Serial logging over the chip's USB Serial/JTAG. With USB plugged into a
// host that does not read the port, HWCDC::write() waits up to 20 x the TX
// timeout per call once its buffer is full, and keeps treating the host as
// connected. At the default 100 ms that stalled the whole boot for minutes
// with a black screen. Logging is best-effort instead: a short timeout, and
// after a short write no logging until kLogRetryMs have passed.
constexpr size_t kLogTxBuffer = 4096;
constexpr uint32_t kLogTxTimeoutMs = 10;
constexpr uint32_t kLogRetryMs = 3000;

CardputerDisplay g_display;
Keyboard_Class g_keyboard;
bool g_isAdv = false;
bool g_enterHeld = false;
bool g_logStalled = false;
uint32_t g_logRetryAt = 0;

// Same passive probe M5GFX uses: the ADV has I2C pull-ups on G8/G9, while on
// v1.x those pins feed the keyboard decoder and read low with a pull-down.
bool detectAdv() {
    pinMode(kPinSysSda, INPUT_PULLDOWN);
    pinMode(kPinSysScl, INPUT_PULLDOWN);
    ::delay(2);
    const bool pulledUp = digitalRead(kPinSysSda) == HIGH && digitalRead(kPinSysScl) == HIGH;
    pinMode(kPinSysSda, INPUT);
    pinMode(kPinSysScl, INPUT);
    return pulledUp;
}

// M5Cardputer 1.1.1 reports printable keys in `word` and a few flags. The
// arrow and Esc legends sit on the fn layer of ; , . / and `.
bool translate(const Keyboard_Class::KeysState &s, KeyEvent &out) {
    if (s.enter) out.key = Key::Enter;
    else if (s.del) out.key = Key::Backspace;
    else if (s.tab) out.key = Key::Tab;
    else if (!s.word.empty()) {
        const char c = s.word.front();
        if (s.fn) {
            switch (c) {
            case ';': out.key = Key::Up; return true;
            case '.': out.key = Key::Down; return true;
            case ',': out.key = Key::Left; return true;
            case '/': out.key = Key::Right; return true;
            case '`': out.key = Key::Escape; return true;
            default: break;
            }
        }
        out.key = Key::Char;
        out.ch = c;
    } else {
        return false; // modifiers only
    }
    return true;
}

const char *resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "USB (host or esptool)";
    default: return "other";
    }
}

} // namespace

void begin() {
    Serial.setTxBufferSize(kLogTxBuffer);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(kLogTxTimeoutMs);

    g_isAdv = detectAdv();

    g_display.init();
    g_display.setRotation(1);
    g_display.setBrightness(160);

    if (g_isAdv) {
        m5::In_I2C.begin(I2C_NUM_0, kPinSysSda, kPinSysScl);
        g_keyboard.begin(std::unique_ptr<KeyboardReader>(new TCA8418KeyboardReader()));
    } else {
        g_keyboard.begin(std::unique_ptr<KeyboardReader>(new IOMatrixKeyboardReader()));
    }

    storageMount();
}

const char *chipModel() { return ESP.getChipModel(); }

// Always starts from scratch, so R on the backup screen also picks up a card
// that was swapped since the last mount.
bool storageMount() {
    g_sd.end(); // also ends the SPI bus; no-op before the first mount
    g_sdMounted = false;
    g_sdHz = 0;
    if (g_isAdv) {
        // On v1.x this pin belongs to the keyboard matrix; leave it alone there.
        pinMode(kPinAdvSdEnable, OUTPUT);
        digitalWrite(kPinAdvSdEnable, HIGH);
    }

    bool cardSeen = false;
    for (const uint32_t hz : {kSdFastHz, kSdSlowHz}) {
        SPI.begin(kPinSdSck, kPinSdMiso, kPinSdMosi, kPinSdCs);
        // USER_SPI_BEGIN: SdFat's own SPI.begin() would move the bus to the default pins.
        if (g_sd.cardBegin(SdSpiConfig(kPinSdCs, DEDICATED_SPI | USER_SPI_BEGIN, hz, &SPI))) {
            cardSeen = true;
            if (g_sd.volumeBegin()) {
                g_sdMounted = true;
                g_sdHz = hz;
                break;
            }
        }
        g_sd.end();
    }

    if (g_sdMounted) {
        g_sdProblem.clear();
    } else if (!cardSeen) {
        g_sdProblem = "no SD card";
    } else {
        // The card itself worked at the lower speed; ask it what it holds.
        SPI.begin(kPinSdSck, kPinSdMiso, kPinSdMosi, kPinSdCs);
        g_sd.cardBegin(SdSpiConfig(kPinSdCs, DEDICATED_SPI | USER_SPI_BEGIN, kSdSlowHz, &SPI));
        g_sdProblem = describeUnmountable();
        g_sd.end();
    }
    return g_sdMounted;
}

bool storageMounted() { return g_sdMounted; }

const char *storageProblem() { return g_sdProblem.c_str(); }

bool storageMakeDirs(const std::string &dir) {
    if (!g_sdMounted) return false;
    return g_sd.exists(dir.c_str()) || g_sd.mkdir(dir.c_str(), true);
}

bool storageExists(const std::string &path) { return g_sdMounted && g_sd.exists(path.c_str()); }

bool storageWrite(const std::string &path, const uint8_t *data, size_t size) {
    if (!g_sdMounted) return false;
    const std::string tmp = path + ".tmp";
    FsFile file = g_sd.open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!file.isOpen()) return false;
    const bool written = file.write(data, size) == size;
    const bool closed = file.close(); // flushes data and the directory entry
    if (!written || !closed) {
        g_sd.remove(tmp.c_str());
        return false;
    }
    // FAT and exFAT renames fail when the target exists.
    if (g_sd.exists(path.c_str()) && !g_sd.remove(path.c_str())) return false;
    return g_sd.rename(tmp.c_str(), path.c_str());
}

bool storageRead(const std::string &path, std::vector<uint8_t> &out) {
    if (!g_sdMounted) return false;
    FsFile file = g_sd.open(path.c_str(), O_RDONLY);
    if (!file.isOpen()) return false;
    out.resize(static_cast<size_t>(file.fileSize()));
    const int got = out.empty() ? 0 : file.read(out.data(), out.size());
    file.close();
    return got == static_cast<int>(out.size());
}

const char *deviceName() { return g_isAdv ? "Cardputer ADV" : "Cardputer"; }

lgfx::LovyanGFX &display() { return g_display; }

bool pollKey(KeyEvent &out) {
    g_keyboard.updateKeyList();
    if (!g_keyboard.isChange()) return false;
    // Update on releases too, so isHeld() sees Enter going up.
    g_keyboard.updateKeysState();
    g_enterHeld = g_keyboard.keysState().enter;
    if (!g_keyboard.isPressed()) return false;
    return translate(g_keyboard.keysState(), out);
}

bool isHeld(Key key) { return key == Key::Enter && g_enterHeld; }

uint32_t millis() { return ::millis(); }

void delay(uint32_t ms) { ::delay(ms); }

void restart() {
    logf("BOOT", "restart requested");
    g_display.setBrightness(0);
    Serial.flush();
    esp_restart();
}

void logf(const char *tag, const char *fmt, ...) {
    if (g_logStalled && ::millis() < g_logRetryAt) return;

    char line[192];
    int n = snprintf(line, sizeof line, "[%s] ", tag);
    va_list args;
    va_start(args, fmt);
    n += vsnprintf(line + n, sizeof line - n, fmt, args);
    va_end(args);
    if (n > static_cast<int>(sizeof line) - 2) n = sizeof line - 2;
    line[n++] = '\n';
    line[n] = '\0';

    const size_t written = Serial.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(n));
    g_logStalled = written < static_cast<size_t>(n);
    if (g_logStalled) g_logRetryAt = ::millis() + kLogRetryMs;
}

void logBootDiagnostics() {
    const esp_reset_reason_t reason = esp_reset_reason();
    logf("BOOT", "reset reason: %s (%d)", resetReasonName(reason), static_cast<int>(reason));
#if NVSM_NO_GUARD
    // Control build used only to prove the guard on a test device.
    logf("NVS", "boot guard DISABLED in this build");
#else
    const nvsguard::BootReport r = nvsguard::bootReport();
    logf("NVS", "boot init %s: %s (0x%x), full erases blocked: %u", r.initSeen ? "seen" : "not seen",
         esp_err_to_name(r.initResult), static_cast<unsigned>(r.initResult), static_cast<unsigned>(r.blockedErases));
#endif
    logf("BOOT", "keyboard: %s", g_isAdv ? "TCA8418 (ADV)" : "GPIO matrix (v1.x)");
    if (g_sdMounted) {
        const uint64_t cardBytes = static_cast<uint64_t>(g_sd.card()->sectorCount()) * 512;
        logf("SD", "mounted %s, card %u MiB, cluster %u B, SPI %u MHz", volumeName(g_sd.fatType()),
             static_cast<unsigned>(cardBytes / (1024 * 1024)), static_cast<unsigned>(g_sd.bytesPerCluster()),
             static_cast<unsigned>(g_sdHz / 1000000));
    } else {
        logf("SD", "unavailable: %s", g_sdProblem.c_str());
    }
}

} // namespace platform

#include "platform/Platform.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <idf_host_shim.h>

namespace platform {
namespace {

M5GFX g_display;

// SDL delivers events on the main thread; the application polls from its
// worker thread. An event watch copies key presses into this queue.
std::mutex g_keyMutex;
std::deque<KeyEvent> g_keys;
constexpr size_t kMaxQueuedKeys = 32;
std::atomic<bool> g_enterHeld{false};

const auto g_start = std::chrono::steady_clock::now();

std::string g_nvsImageNote;
bool g_sdMounted = false;
std::string g_sdRoot;

bool isEnter(SDL_Keycode sym) { return sym == SDLK_RETURN || sym == SDLK_KP_ENTER; }

bool translate(const SDL_Event &e, KeyEvent &out) {
    if (e.type == SDL_KEYDOWN) {
        // The Cardputer keyboard has no auto-repeat; without this filter a held
        // Enter would confirm a change the moment its confirmation opens.
        if (e.key.repeat != 0) return false;
        switch (e.key.keysym.sym) {
        case SDLK_UP: out.key = Key::Up; return true;
        case SDLK_DOWN: out.key = Key::Down; return true;
        case SDLK_LEFT: out.key = Key::Left; return true;
        case SDLK_RIGHT: out.key = Key::Right; return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: out.key = Key::Enter; return true;
        case SDLK_ESCAPE: out.key = Key::Escape; return true;
        case SDLK_BACKSPACE: out.key = Key::Backspace; return true;
        case SDLK_TAB: out.key = Key::Tab; return true;
        default: return false;
        }
    }
    if (e.type == SDL_TEXTINPUT) {
        const unsigned char c = static_cast<unsigned char>(e.text.text[0]);
        if (c < 0x20 || c > 0x7E || e.text.text[1] != '\0') return false; // ASCII only, like the device
        out.key = Key::Char;
        out.ch = static_cast<char>(c);
        return true;
    }
    return false;
}

int keyWatch(void *, SDL_Event *e) {
    if ((e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && isEnter(e->key.keysym.sym)) {
        g_enterHeld = e->type == SDL_KEYDOWN;
    }
    KeyEvent ev;
    if (translate(*e, ev)) {
        std::lock_guard<std::mutex> lock(g_keyMutex);
        if (g_keys.size() < kMaxQueuedKeys) g_keys.push_back(ev);
    }
    return 0;
}

// Registers the NVS dump named by NVSM_NVS_IMAGE as partition "nvs". The file
// is copied into memory; nothing is ever written back to it.
void loadNvsImage() {
    const char *path = std::getenv("NVSM_NVS_IMAGE");
    if (path == nullptr || *path == '\0') {
        g_nvsImageNote = "no NVS image: set NVSM_NVS_IMAGE to a raw partition dump";
        return;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        g_nvsImageNote = std::string("cannot open NVS image ") + path;
        return;
    }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const esp_err_t err = idf_host_register_nvs_image("nvs", data.data(), data.size());
    if (err != ESP_OK) {
        g_nvsImageNote = std::string("NVS image rejected (") + esp_err_to_name(err) + "): " + path;
        return;
    }
    g_nvsImageNote = "NVS image " + std::string(path) + " (" + std::to_string(data.size()) + " B, RAM copy)";
}

} // namespace

void begin() {
    g_display.init();
    SDL_AddEventWatch(keyWatch, nullptr);
    loadNvsImage();
    storageMount();
}

const char *chipModel() { return "desktop"; }

// The desktop stands in for the SD card with a directory (NVSM_SD_DIR).
bool storageMount() {
    const char *dir = std::getenv("NVSM_SD_DIR");
    std::error_code ec;
    g_sdMounted = dir != nullptr && *dir != '\0' && std::filesystem::is_directory(dir, ec);
    g_sdRoot = g_sdMounted ? dir : "";
    return g_sdMounted;
}

bool storageMounted() { return g_sdMounted; }

const char *storageProblem() { return g_sdMounted ? "" : "no SD directory: set NVSM_SD_DIR"; }

bool storageMakeDirs(const std::string &dir) {
    std::error_code ec;
    if (!g_sdMounted) return false;
    std::filesystem::create_directories(g_sdRoot + dir, ec);
    return !ec;
}

bool storageExists(const std::string &path) {
    std::error_code ec;
    return g_sdMounted && std::filesystem::exists(g_sdRoot + path, ec);
}

bool storageWrite(const std::string &path, const uint8_t *data, size_t size) {
    if (!g_sdMounted) return false;
    const std::string target = g_sdRoot + path;
    const std::string tmp = target + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    return !ec;
}

bool storageRead(const std::string &path, std::vector<uint8_t> &out) {
    if (!g_sdMounted) return false;
    std::ifstream in(g_sdRoot + path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool storageList(const std::string &dir, std::vector<std::string> &names) {
    names.clear();
    if (!g_sdMounted) return false;
    std::error_code ec;
    std::filesystem::directory_iterator it(g_sdRoot + dir, ec);
    for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec)) names.push_back(it->path().filename().string());
    }
    return !ec;
}

// NVSM_DEVICE_ID lets a test pretend to be another device.
std::string deviceId() {
    const char *id = std::getenv("NVSM_DEVICE_ID");
    return id != nullptr && *id != '\0' ? id : "desktop";
}

const char *deviceName() { return "Desktop (SDL)"; }

lgfx::LovyanGFX &display() { return g_display; }

bool pollKey(KeyEvent &out) {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    if (g_keys.empty()) return false;
    out = g_keys.front();
    g_keys.pop_front();
    return true;
}

bool isHeld(Key key) { return key == Key::Enter && g_enterHeld.load(); }

uint32_t millis() {
    const auto elapsed = std::chrono::steady_clock::now() - g_start;
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void restart() {
    logf("BOOT", "exit requested");
    std::exit(0);
}

void logf(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::printf("[%s] ", tag);
    std::vprintf(fmt, args);
    std::printf("\n");
    std::fflush(stdout);
    va_end(args);
}

void logBootDiagnostics() {
    logf("NVS", "%s", g_nvsImageNote.c_str());
    if (g_sdMounted) {
        logf("SD", "directory %s", g_sdRoot.c_str());
    } else {
        logf("SD", "unavailable: set NVSM_SD_DIR to an existing directory");
    }
}

} // namespace platform

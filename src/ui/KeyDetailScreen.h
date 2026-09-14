#pragma once

#include <string>
#include <vector>

#include <esp_err.h>

#include "nvs/NvsTypes.h"
#include "ui/Screen.h"

namespace ui {

// One key. Strings and blobs stay hidden until the user asks to reveal them,
// and are never logged.
class KeyDetailScreen : public Screen {
public:
    KeyDetailScreen(UiController &ui, std::string ns, nvsm::KeyInfo key);

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;
    void onModelRefreshed() override { load(); }

private:
    void load();
    std::vector<std::string> valueLines(lgfx::LovyanGFX &g, int width) const;

    std::string ns_;
    nvsm::KeyInfo key_;
    nvsm::Value value_;
    esp_err_t error_ = ESP_OK;
    bool revealed_ = false;
    size_t scroll_ = 0;
    size_t lastLineCount_ = 0;
    size_t lastVisibleLines_ = 0;
};

} // namespace ui

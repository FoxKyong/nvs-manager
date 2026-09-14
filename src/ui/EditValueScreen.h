#pragma once

#include <string>

#include <esp_err.h>

#include "nvs/NvsTypes.h"
#include "ui/Screen.h"

namespace ui {

// Types a new value for an integer or string key and checks it against the
// key's type before asking for confirmation. A string's current value stays
// hidden; an integer's is the starting text.
class EditValueScreen : public Screen {
public:
    EditValueScreen(UiController &ui, std::string ns, nvsm::KeyInfo key);

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;
    bool textMode() const override { return true; }

private:
    bool parse(nvsm::Value &out);
    std::string currentText() const;
    size_t maxLength() const;

    std::string ns_;
    nvsm::KeyInfo key_;
    nvsm::Value current_;
    esp_err_t readError_ = ESP_OK;
    std::string text_;
    std::string problem_;
};

} // namespace ui

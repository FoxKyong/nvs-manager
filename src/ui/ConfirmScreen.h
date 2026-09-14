#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/Policy.h"
#include "ui/Screen.h"

namespace ui {

struct ConfirmRequest {
    std::string title;
    std::vector<std::string> lines; // sentences, wrapped to the display
    std::string warning;            // highlighted; may be empty
    app::Confirmation confirmation = app::Confirmation::Press;
    // Runs once confirmed, after this screen has been popped.
    std::function<void(UiController &)> onConfirm;
};

// Asks before a change. Enter only counts after it has been seen released
// since the screen opened, so the key press that led here cannot confirm.
class ConfirmScreen : public Screen {
public:
    ConfirmScreen(UiController &ui, ConfirmRequest request);

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;
    void tick() override;

private:
    void confirm();

    ConfirmRequest request_;
    bool armed_ = false;
    bool holding_ = false;
    bool done_ = false;
    uint32_t holdStart_ = 0;
    int progress_ = 0; // percent of the required hold
};

} // namespace ui

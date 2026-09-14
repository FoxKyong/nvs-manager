#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ui/Screen.h"

namespace ui {

// What the numbers mean and what deleting costs; scrollable text, wrapped to
// the display width on first draw.
class HelpScreen : public Screen {
public:
    explicit HelpScreen(UiController &ui) : Screen(ui) {}

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;

private:
    std::vector<std::string> lines_;
    size_t scroll_ = 0;
    size_t visibleLines_ = 0;
};

} // namespace ui

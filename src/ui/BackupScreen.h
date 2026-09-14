#pragma once

#include <cstddef>

#include "ui/Screen.h"

namespace ui {

// Exports and raw backup to the SD card. Nothing here changes NVS.
class BackupScreen : public Screen {
public:
    explicit BackupScreen(UiController &ui) : Screen(ui) {}

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;

private:
    void run(size_t option);

    size_t selected_ = 0;
};

} // namespace ui

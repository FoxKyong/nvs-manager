#pragma once

#include "ui/Screen.h"

namespace ui {

// Capacity at a glance: status, available entries, usage bar, totals.
class DashboardScreen : public Screen {
public:
    explicit DashboardScreen(UiController &ui) : Screen(ui) {}

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;

private:
    void drawError(lgfx::LovyanGFX &g);
};

} // namespace ui

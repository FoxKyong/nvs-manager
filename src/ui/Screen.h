#pragma once

#include <cstdio> // M5GFX's DataWrapper.hpp uses fopen() without including it

#include <M5GFX.h>

#include "ui/Input.h"

namespace ui {

class UiController;

// One full screen. Screens only draw into the canvas they are given and read
// NVS through the model; they never touch the NVS API directly.
class Screen {
public:
    virtual ~Screen() = default;

    virtual void draw(lgfx::LovyanGFX &g) = 0;
    virtual void handle(const Input &in) = 0;

    // True while the screen takes typed text rather than commands.
    virtual bool textMode() const { return false; }

    // Called on every screen in the stack after the model was re-read.
    virtual void onModelRefreshed() {}

    // Called on the top screen once per main-loop pass, for time-based work
    // such as hold-to-confirm.
    virtual void tick() {}

protected:
    explicit Screen(UiController &ui) : ui_(ui) {}
    UiController &ui_;
};

} // namespace ui

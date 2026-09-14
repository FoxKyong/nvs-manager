#pragma once

#include <cstdio> // M5GFX's DataWrapper.hpp uses fopen() without including it
#include <memory>
#include <vector>

#include <M5GFX.h>

#include "app/NvsModel.h"
#include "platform/Platform.h"
#include "ui/Screen.h"

namespace ui {

// Owns the screen stack, an off-screen canvas and the runtime-only Advanced
// mode. A frame is pushed to the display in one go, so screens never flicker.
class UiController {
public:
    explicit UiController(app::NvsModel &model);

    void begin();
    void handleKey(const platform::KeyEvent &ev);
    void update(); // once per loop: Advanced mode timeout, screen tick, stack changes
    void render(); // draws only when something changed

    // Take effect after the current key or tick, so a screen may push or pop
    // itself safely. Pops are applied before pushes.
    void push(std::unique_ptr<Screen> screen);
    void pop();

    void invalidate() { dirty_ = true; }
    void refreshModel(); // re-reads NVS, then modelChanged()
    void modelChanged(); // tells every screen the model has new data

    // Advanced mode unlocks SYSTEM namespaces. It lives only in RAM: a reboot
    // or config::kAdvancedIdleLockMs without input locks it again.
    bool advanced() const { return advanced_; }
    void setAdvanced(bool on);

    app::NvsModel &model() { return model_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void applyPending();

    app::NvsModel &model_;
    lgfx::LGFX_Sprite canvas_;
    int width_ = 0;
    int height_ = 0;
    bool haveCanvas_ = false;
    std::vector<std::unique_ptr<Screen>> stack_;
    std::vector<std::unique_ptr<Screen>> pendingPush_;
    int pendingPops_ = 0;
    bool dirty_ = true;
    bool advanced_ = false;
    uint32_t lastInputMs_ = 0;
};

} // namespace ui

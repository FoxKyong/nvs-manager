#pragma once

#include <string>
#include <vector>

#include "app/NvsModel.h"
#include "ui/Screen.h"

namespace ui {

struct ResultInfo {
    bool ok = false;
    std::string title;
    std::string badge; // e.g. "DONE", "FAILED", "LOCKED"
    std::vector<std::string> lines;
};

// Success only when the change was committed and read back; otherwise the
// failing step with the error's name and code. Capacity figures come from
// stats read before and after, never from an estimate.
ResultInfo describeChange(const std::string &successTitle, const app::ChangeOutcome &outcome);

class ResultScreen : public Screen {
public:
    ResultScreen(UiController &ui, ResultInfo info) : Screen(ui), info_(std::move(info)) {}

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;

private:
    ResultInfo info_;
};

} // namespace ui

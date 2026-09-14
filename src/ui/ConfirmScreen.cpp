#include "ui/ConfirmScreen.h"

#include <algorithm>

#include "app/Config.h"
#include "platform/Platform.h"
#include "ui/Draw.h"
#include "ui/UiController.h"

namespace ui {

ConfirmScreen::ConfirmScreen(UiController &ui, ConfirmRequest request) : Screen(ui), request_(std::move(request)) {}

void ConfirmScreen::tick() {
    if (done_) return;
    const bool held = platform::isHeld(platform::Key::Enter);
    if (!armed_) {
        if (!held) armed_ = true;
        return;
    }
    if (request_.confirmation != app::Confirmation::Hold) return;

    if (!held) {
        if (holding_ || progress_ != 0) {
            holding_ = false;
            progress_ = 0;
            ui_.invalidate();
        }
        return;
    }

    const uint32_t now = platform::millis();
    if (!holding_) {
        holding_ = true;
        holdStart_ = now;
    }
    const uint32_t elapsed = now - holdStart_;
    const int progress =
        static_cast<int>(std::min(elapsed, app::config::kHoldConfirmMs) * 100 / app::config::kHoldConfirmMs);
    if (progress != progress_) {
        progress_ = progress;
        ui_.invalidate();
    }
    if (elapsed >= app::config::kHoldConfirmMs) confirm();
}

void ConfirmScreen::handle(const Input &in) {
    if (done_) return;
    if (in.nav == Nav::Back) {
        ui_.pop();
        return;
    }
    if (in.nav == Nav::Enter && armed_ && request_.confirmation == app::Confirmation::Press) confirm();
}

void ConfirmScreen::confirm() {
    done_ = true;
    ui_.pop();
    if (request_.onConfirm) request_.onConfirm(ui_);
}

void ConfirmScreen::draw(lgfx::LovyanGFX &g) {
    const int w = g.width();
    const int h = g.height();
    const bool hold = request_.confirmation == app::Confirmation::Hold;

    drawHeader(g, request_.title);

    g.setFont(theme::fontSmall());
    g.setTextDatum(textdatum_t::top_left);
    const int bottom = h - theme::kFooterH - (hold ? 14 : 2);
    int y = theme::kHeaderH + 3;
    const auto print = [&](const std::string &text, uint32_t color) {
        g.setTextColor(color);
        for (const auto &line : wrapText(g, text, w - 8)) {
            if (y + theme::kSmallRowH > bottom) return;
            g.drawString(line.c_str(), 4, y);
            y += theme::kSmallRowH;
        }
    };
    for (const auto &line : request_.lines) print(line, theme::kText);
    if (!request_.warning.empty()) {
        y += 3;
        print(request_.warning, theme::kHealthLow);
    }

    if (hold) {
        const int barY = h - theme::kFooterH - 10;
        g.fillRect(4, barY, w - 8, 6, theme::kBarTrack);
        g.fillRect(4, barY, (w - 8) * progress_ / 100, 6, theme::kHealthFull);
    }
    drawFooter(g, hold ? "Hold Enter: confirm   Esc: cancel" : "Enter: confirm   Esc: cancel");
}

} // namespace ui

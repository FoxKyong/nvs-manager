#include "ui/ResultScreen.h"

#include <cstdio>

#include <nvs.h>

#include "ui/Draw.h"
#include "ui/UiController.h"

namespace ui {

ResultInfo describeChange(const std::string &successTitle, const app::ChangeOutcome &outcome) {
    ResultInfo r;
    char buf[96];
    r.ok = outcome.result.ok();
    if (r.ok) {
        r.title = successTitle;
        r.badge = "DONE";
        r.lines.push_back("Committed and verified by reading it back.");
    } else {
        r.title = "Change failed";
        r.badge = "FAILED";
        snprintf(buf, sizeof buf, "Failed at: %s", outcome.result.step);
        r.lines.push_back(buf);
        snprintf(buf, sizeof buf, "%s (0x%x)", esp_err_to_name(outcome.result.err),
                 static_cast<unsigned>(outcome.result.err));
        r.lines.push_back(buf);
        if (outcome.result.err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
            r.lines.push_back("NVS writes the new value before freeing the old, so it needs free entries. "
                              "Delete something first.");
        }
    }

    if (outcome.statsValid) {
        const long freed =
            static_cast<long>(outcome.before.usedEntries) - static_cast<long>(outcome.after.usedEntries);
        if (freed > 0) {
            snprintf(buf, sizeof buf, "Freed entries: %ld", freed);
            r.lines.push_back(buf);
        } else if (freed < 0) {
            snprintf(buf, sizeof buf, "Uses %ld more entries", -freed);
            r.lines.push_back(buf);
        }
        snprintf(buf, sizeof buf, "Available: %u -> %u", static_cast<unsigned>(outcome.before.availableEntries),
                 static_cast<unsigned>(outcome.after.availableEntries));
        r.lines.push_back(buf);
    } else {
        r.lines.push_back("The NVS state could not be read afterwards.");
    }
    return r;
}

void ResultScreen::draw(lgfx::LovyanGFX &g) {
    const int w = g.width();
    drawHeader(g, info_.title);

    int y = theme::kHeaderH + 4;
    drawBadge(g, 4, y, 14, info_.badge.c_str(), info_.ok ? theme::kHealthOk : theme::kHealthFull, false);
    y += 20;

    g.setFont(theme::fontSmall());
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(theme::kText);
    const int bottom = g.height() - theme::kFooterH - 2;
    for (const auto &text : info_.lines) {
        for (const auto &line : wrapText(g, text, w - 8)) {
            if (y + theme::kSmallRowH > bottom) break;
            g.drawString(line.c_str(), 4, y);
            y += theme::kSmallRowH;
        }
    }
    drawFooter(g, "Enter or Esc: back");
}

void ResultScreen::handle(const Input &in) {
    if (in.nav == Nav::Enter || in.nav == Nav::Back) ui_.pop();
}

} // namespace ui

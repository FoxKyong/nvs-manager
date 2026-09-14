#include "ui/HelpScreen.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "ui/Draw.h"
#include "ui/UiController.h"

namespace ui {
namespace {

// Paragraphs; an empty string is a blank line.
constexpr const char *kParagraphs[] = {
    "NVS keeps settings in 32-byte entries, grouped in 4 KiB pages.",
    "",
    "Namespace: the keys of one application. The namespace itself also takes an entry.",
    "",
    "Free: entries without data, deleted ones included.",
    "Available: what new data can still use. NVS always keeps one page free for its garbage collection, "
    "so it can be FULL while a page is physically empty.",
    "",
    "Namespaces without keys and entries of deleted namespaces still take space but cannot be listed.",
    "",
    "Changes: E edits a number or text, D deletes a key or a whole namespace. APP namespaces need Enter, "
    "LAUNCHER ones a held Enter. SYSTEM ones need Advanced mode (A on the dashboard) and a held Enter; "
    "it locks after 5 minutes without a key press and on reboot.",
    "",
    "Deleting a namespace resets the settings of the application that owns it.",
    "",
    "Backup (B on the dashboard) writes to /NVSManager on the SD card: a metadata export without values, "
    "an export with values, or a raw copy of the partition with a SHA-256 manifest. Files are numbered because "
    "the device has no clock. A raw backup or a values export can contain passwords and tokens. Keep them private.",
    "",
    "Keys",
    "; .  move      , /  page",
    "Enter  open    Esc Del  back",
    "/  find    S  sort    R  refresh",
    "V  show a hidden value",
    "E  edit    D  delete    A  advanced",
    "B  backup and export",
    "Esc on the dashboard: exit (restart)",
};

} // namespace

void HelpScreen::draw(lgfx::LovyanGFX &g) {
    g.setFont(theme::fontSmall());
    if (lines_.empty()) {
        // About. The fonts cover ASCII only, so the author appears under a nickname.
        const std::string about[] = {
            std::string("NVS Manager ") + NVSM_VERSION + " by FoxKyong",
            "MIT licence, no warranty. github.com/FoxKyong/nvs-manager",
            "",
        };
        for (const std::string &paragraph : about) {
            for (auto &line : wrapText(g, paragraph, g.width() - 8)) lines_.push_back(std::move(line));
        }
        for (const char *paragraph : kParagraphs) {
            for (auto &line : wrapText(g, paragraph, g.width() - 8)) lines_.push_back(std::move(line));
        }
    }

    const int top = theme::kHeaderH + 2;
    visibleLines_ = static_cast<size_t>((g.height() - theme::kFooterH - top) / theme::kSmallRowH);
    const size_t maxScroll = lines_.size() > visibleLines_ ? lines_.size() - visibleLines_ : 0;
    scroll_ = std::min(scroll_, maxScroll);

    g.setFont(theme::fontSmall());
    g.setTextColor(theme::kText);
    g.setTextDatum(textdatum_t::top_left);
    for (size_t i = 0; i < visibleLines_ && scroll_ + i < lines_.size(); ++i) {
        g.drawString(lines_[scroll_ + i].c_str(), 4, top + static_cast<int>(i) * theme::kSmallRowH);
    }

    char right[16];
    snprintf(right, sizeof right, "%u/%u", static_cast<unsigned>(scroll_ + 1), static_cast<unsigned>(lines_.size()));
    drawHeader(g, "Help", right);
    drawFooter(g, "; . , /:scroll  Esc:back");
}

void HelpScreen::handle(const Input &in) {
    ui_.invalidate();
    const size_t maxScroll = lines_.size() > visibleLines_ ? lines_.size() - visibleLines_ : 0;
    switch (in.nav) {
    case Nav::Back: ui_.pop(); break;
    case Nav::Up:
        if (scroll_ > 0) --scroll_;
        break;
    case Nav::Down:
        if (scroll_ < maxScroll) ++scroll_;
        break;
    case Nav::PageUp: scroll_ = scroll_ > visibleLines_ ? scroll_ - visibleLines_ : 0; break;
    case Nav::PageDown: scroll_ = std::min(maxScroll, scroll_ + visibleLines_); break;
    default: break;
    }
}

} // namespace ui

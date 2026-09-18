#include "ui/RestoreScreen.h"

#include <algorithm>
#include <cstdio>
#include <memory>

#include "ui/ConfirmScreen.h"
#include "ui/Draw.h"
#include "ui/ResultScreen.h"
#include "ui/UiController.h"

namespace ui {
namespace {

std::string statsText(const nvsm::Stats &s) {
    char buf[80];
    snprintf(buf, sizeof buf, "used %u, available %u, %u namespaces", static_cast<unsigned>(s.usedEntries),
             static_cast<unsigned>(s.availableEntries), static_cast<unsigned>(s.namespaceCount));
    return buf;
}

std::string fileName(const std::string &path) { return path.substr(path.find_last_of('/') + 1); }

ResultInfo describeRestore(const app::RestoreResult &r) {
    if (!r.attempted) {
        ResultInfo info;
        info.title = "Nothing restored";
        info.badge = "FAILED";
        info.lines.push_back("The current NVS could not be saved first: " + r.safety.error + ".");
        info.lines.push_back("NVS was not changed.");
        return info;
    }
    const std::string saved = r.safety.files.empty() ? std::string("?") : fileName(r.safety.files.front());
    if (!r.change.result.ok()) {
        ResultInfo info = describeChange("", r.change);
        info.title = "Restore failed";
        info.lines.push_back("NVS as it was before is saved as " + saved + "; restore that to go back.");
        return info;
    }
    // Shorter than describeChange(), so everything fits on the screen.
    ResultInfo info;
    info.ok = true;
    info.title = "NVS restored";
    info.badge = "DONE";
    info.lines.push_back("Written and verified by reading it back.");
    if (r.change.statsValid) {
        char buf[64];
        snprintf(buf, sizeof buf, "Available: %u -> %u", static_cast<unsigned>(r.change.before.availableEntries),
                 static_cast<unsigned>(r.change.after.availableEntries));
        info.lines.push_back(buf);
    }
    info.lines.push_back("The NVS before: " + saved);
    return info;
}

} // namespace

RestoreListScreen::RestoreListScreen(UiController &ui) : Screen(ui) {
    if (!app::listBackups(backups_, error_) && error_.empty()) error_ = "SD card not available";
    cursor_.reset(backups_.size(), 0);
}

size_t RestoreListScreen::visibleRows() const {
    const int h = ui_.height() - theme::kFooterH - theme::kHeaderH;
    return h > 0 ? static_cast<size_t>(h / theme::kRowH) : 0;
}

void RestoreListScreen::handle(const Input &in) {
    const size_t rows = visibleRows();
    ui_.invalidate();
    switch (in.nav) {
    case Nav::Up: cursor_.move(-1, rows); break;
    case Nav::Down: cursor_.move(1, rows); break;
    case Nav::PageUp: cursor_.move(-static_cast<long>(rows), rows); break;
    case Nav::PageDown: cursor_.move(static_cast<long>(rows), rows); break;
    case Nav::Enter:
        if (!cursor_.empty()) {
            ui_.push(std::unique_ptr<Screen>(new RestorePreviewScreen(ui_, backups_[cursor_.selected()])));
        }
        break;
    case Nav::Back: ui_.pop(); break;
    default: break;
    }
}

void RestoreListScreen::draw(lgfx::LovyanGFX &g) {
    const int w = g.width();
    char buf[64];
    snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(backups_.size()));
    drawHeader(g, "Restore raw backup", buf);

    const int y = theme::kHeaderH;
    const size_t rows = visibleRows();
    cursor_.follow(rows);

    g.setTextDatum(textdatum_t::middle_left);
    if (!error_.empty() || backups_.empty()) {
        g.setFont(theme::fontSmall());
        g.setTextColor(error_.empty() ? theme::kTextDim : theme::kHealthFull);
        const std::string text = error_.empty() ? "No raw backups in /NVSManager/backups yet." : error_;
        int lineY = y + 4;
        for (const auto &line : wrapText(g, text, w - 8)) {
            g.setTextDatum(textdatum_t::top_left);
            g.drawString(line.c_str(), 4, lineY);
            lineY += theme::kSmallRowH;
        }
    }

    for (size_t r = 0; r < rows && cursor_.top() + r < backups_.size(); ++r) {
        const size_t index = cursor_.top() + r;
        const app::BackupInfo &b = backups_[index];
        const int rowY = y + static_cast<int>(r) * theme::kRowH;
        const int midY = rowY + theme::kRowH / 2 + 1;
        if (index == cursor_.selected()) g.fillRect(0, rowY, w, theme::kRowH, theme::kSelection);

        g.setFont(theme::fontSmall());
        g.setTextDatum(textdatum_t::middle_right);
        if (!b.hasManifest) {
            g.setTextColor(theme::kHealthFull);
            snprintf(buf, sizeof buf, "no manifest");
        } else if (b.haveStats) {
            g.setTextColor(theme::kTextDim);
            snprintf(buf, sizeof buf, "used %u  avail %u", static_cast<unsigned>(b.stats.usedEntries),
                     static_cast<unsigned>(b.stats.availableEntries));
        } else {
            buf[0] = '\0';
        }
        g.drawString(buf, w - 4, midY);

        g.setFont(theme::fontNormal());
        g.setTextColor(theme::kText);
        g.setTextDatum(textdatum_t::middle_left);
        g.drawString(b.name.c_str(), 4, midY);
    }

    drawFooter(g, backups_.empty() ? "Esc:back" : "Enter:check  Esc:back");
}

RestorePreviewScreen::RestorePreviewScreen(UiController &ui, app::BackupInfo backup)
    : Screen(ui), backup_(std::move(backup)), check_(app::checkBackup(ui.model(), backup_)) {
    const app::NvsModel &m = ui_.model();
    // What blocks or needs attention first, so it shows without scrolling.
    for (const auto &p : check_.problems) paragraphs_.push_back({theme::kHealthFull, p});
    for (const auto &w : check_.warnings) paragraphs_.push_back({theme::kHealthLow, w});
    if (!paragraphs_.empty()) paragraphs_.push_back({theme::kText, ""});
    if (backup_.hasManifest) {
        std::string made = "Backup, by NVS Manager " + (backup_.toolVersion.empty() ? "?" : backup_.toolVersion);
        if (!backup_.device.empty()) made += " on " + backup_.device;
        paragraphs_.push_back({theme::kTextDim, made + ":"});
        paragraphs_.push_back({theme::kText, backup_.haveStats ? statsText(backup_.stats) : "no statistics recorded"});
    }
    paragraphs_.push_back({theme::kTextDim, "NVS now:"});
    paragraphs_.push_back({theme::kText, m.error() == ESP_OK ? statsText(m.stats()) : "cannot be read"});
    if (check_.ok) {
        paragraphs_.push_back({theme::kText, ""});
        paragraphs_.push_back({theme::kText, "Restoring replaces the whole NVS partition with this backup. "
                                             "The current NVS is saved as a new backup first."});
    }
}

void RestorePreviewScreen::confirm() {
    ConfirmRequest req;
    req.title = "Restore " + backup_.name + "?";
    req.lines = {"Every namespace is replaced by the backup, Wi-Fi and Launcher settings included.",
                 "The current NVS is saved to the SD card first, so this can be undone."};
    req.warning = "Do not switch the device off or unplug it while it runs.";
    req.confirmation = app::Confirmation::Hold;
    const app::BackupInfo backup = backup_;
    const app::RestoreCheck check = check_;
    req.onConfirm = [backup, check](UiController &ui) {
        const app::RestoreResult r = app::restoreBackup(ui.model(), backup, check);
        ui.modelChanged();
        ui.pop(); // this preview
        ui.pop(); // the backup list, which no longer shows the new safety backup
        ui.push(std::unique_ptr<Screen>(new ResultScreen(ui, describeRestore(r))));
    };
    ui_.push(std::unique_ptr<Screen>(new ConfirmScreen(ui_, std::move(req))));
}

void RestorePreviewScreen::handle(const Input &in) {
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
    case Nav::Enter:
        if (check_.ok) confirm();
        break;
    default: break;
    }
}

void RestorePreviewScreen::draw(lgfx::LovyanGFX &g) {
    g.setFont(theme::fontSmall());
    if (lines_.empty()) {
        for (const auto &p : paragraphs_) {
            for (auto &line : wrapText(g, p.second, g.width() - 8)) lines_.push_back({p.first, std::move(line)});
        }
    }
    drawHeader(g, "Restore " + backup_.name, check_.ok ? "READY" : "BLOCKED");

    const int top = theme::kHeaderH + 2;
    visibleLines_ = static_cast<size_t>((g.height() - theme::kFooterH - top) / theme::kSmallRowH);
    const size_t maxScroll = lines_.size() > visibleLines_ ? lines_.size() - visibleLines_ : 0;
    scroll_ = std::min(scroll_, maxScroll);

    g.setFont(theme::fontSmall());
    g.setTextDatum(textdatum_t::top_left);
    for (size_t i = 0; i < visibleLines_ && scroll_ + i < lines_.size(); ++i) {
        g.setTextColor(lines_[scroll_ + i].first);
        g.drawString(lines_[scroll_ + i].second.c_str(), 4, top + static_cast<int>(i) * theme::kSmallRowH);
    }
    drawFooter(g, check_.ok ? "Enter:restore  ; .:scroll  Esc:back" : "; .:scroll  Esc:back");
}

} // namespace ui

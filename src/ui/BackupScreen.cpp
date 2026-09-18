#include "ui/BackupScreen.h"

#include <cstdio>
#include <memory>

#include "app/Export.h"
#include "platform/Platform.h"
#include "ui/ConfirmScreen.h"
#include "ui/Draw.h"
#include "ui/RestoreScreen.h"
#include "ui/ResultScreen.h"
#include "ui/UiController.h"

namespace ui {
namespace {

struct Option {
    const char *title;
    const char *detail;
};

constexpr Option kOptions[] = {
    {"Export metadata", "JSON: namespaces, keys, types, sizes. No values."},
    {"Export with values", "JSON with every readable value. Sensitive."},
    {"Raw backup", "The partition byte for byte, with a SHA-256 manifest."},
    {"Restore raw backup", "Write a raw backup back into NVS."},
};
constexpr size_t kOptionCount = sizeof kOptions / sizeof kOptions[0];

void showExportResult(UiController &ui, const std::string &title, const app::ExportResult &r) {
    ResultInfo info;
    info.ok = r.ok;
    if (r.ok) {
        info.title = title;
        info.badge = "DONE";
        for (const auto &f : r.files) info.lines.push_back(f);
        info.lines.push_back("Size: " + formatBytes(r.bytes));
        info.lines.push_back("SHA-256: " + r.sha256.substr(0, 16) + "...");
        info.lines.push_back("Written and compared by reading it back.");
    } else {
        info.title = "Not written";
        info.badge = "FAILED";
        info.lines.push_back(r.error);
    }
    ui.push(std::unique_ptr<Screen>(new ResultScreen(ui, std::move(info))));
}

} // namespace

void BackupScreen::run(size_t option) {
    if (!platform::storageMounted()) {
        ResultInfo info;
        info.title = "SD not usable";
        info.badge = "SD";
        info.lines.push_back(std::string(platform::storageProblem()) + ".");
        info.lines.push_back("Fix it and press R here to try again. NVS Manager never formats a card.");
        ui_.push(std::unique_ptr<Screen>(new ResultScreen(ui_, std::move(info))));
        return;
    }

    switch (option) {
    case 0: showExportResult(ui_, "Metadata exported", app::exportJson(ui_.model(), false)); break;
    case 1: {
        ConfirmRequest req;
        req.title = "Export values?";
        req.lines = {"Writes every readable value to the SD card as plain text."};
        req.warning = "The file will contain Wi-Fi passwords, tokens and other credentials. Keep the card private.";
        req.confirmation = app::Confirmation::Hold;
        req.onConfirm = [](UiController &ui) {
            showExportResult(ui, "Values exported", app::exportJson(ui.model(), true));
        };
        ui_.push(std::unique_ptr<Screen>(new ConfirmScreen(ui_, std::move(req))));
        break;
    }
    case 2: {
        ConfirmRequest req;
        req.title = "Raw backup?";
        req.lines = {"Copies the whole NVS partition to the SD card, with a manifest and SHA-256."};
        req.warning = "Raw NVS backup can contain passwords, tokens and other credentials.";
        req.confirmation = app::Confirmation::Press;
        req.onConfirm = [](UiController &ui) { showExportResult(ui, "Backup written", app::rawBackup(ui.model())); };
        ui_.push(std::unique_ptr<Screen>(new ConfirmScreen(ui_, std::move(req))));
        break;
    }
    case 3: ui_.push(std::unique_ptr<Screen>(new RestoreListScreen(ui_))); break;
    default: break;
    }
}

void BackupScreen::handle(const Input &in) {
    ui_.invalidate();
    switch (in.nav) {
    case Nav::Back: ui_.pop(); break;
    case Nav::Up:
        if (selected_ > 0) --selected_;
        break;
    case Nav::Down:
        if (selected_ + 1 < kOptionCount) ++selected_;
        break;
    case Nav::Enter: run(selected_); break;
    case Nav::Char:
        if (in.ch == 'r' || in.ch == 'R') {
            const bool mounted = platform::storageMount();
            platform::logf("SD", "mount retry: %s", mounted ? "mounted" : "unavailable");
        }
        break;
    default: break;
    }
}

void BackupScreen::draw(lgfx::LovyanGFX &g) {
    const int w = g.width();
    const bool mounted = platform::storageMounted();
    drawHeader(g, "Backup and export", mounted ? "SD ok" : "no SD");

    constexpr int kOptionH = 22; // four options and the status line above the footer
    int y = theme::kHeaderH + 2;
    for (size_t i = 0; i < kOptionCount; ++i) {
        if (i == selected_) g.fillRect(0, y, w, kOptionH, theme::kSelection);
        g.setTextDatum(textdatum_t::top_left);
        g.setFont(theme::fontNormal());
        g.setTextColor(mounted ? theme::kText : theme::kTextDim);
        g.drawString(kOptions[i].title, 4, y);
        g.setFont(theme::fontSmall());
        g.setTextColor(theme::kTextDim);
        g.drawString(clipText(g, kOptions[i].detail, w - 8).c_str(), 4, y + 12);
        y += kOptionH + 1;
    }

    g.setFont(theme::fontSmall());
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(mounted ? theme::kTextDim : theme::kHealthLow);
    const std::string status = mounted ? std::string("Files go to /NVSManager on the card.")
                                       : std::string("SD: ") + platform::storageProblem();
    g.drawString(clipText(g, status, w - 8).c_str(), 4, y + 1);

    drawFooter(g, mounted ? "Enter:run  Esc:back  R:remount" : "R:retry SD  Esc:back");
}

} // namespace ui

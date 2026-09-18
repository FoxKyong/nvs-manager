#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app/Restore.h"
#include "ui/Input.h"
#include "ui/Screen.h"

namespace ui {

// Raw backups on the SD card, newest first. Choosing one checks it.
class RestoreListScreen : public Screen {
public:
    explicit RestoreListScreen(UiController &ui);

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;

private:
    size_t visibleRows() const;

    std::vector<app::BackupInfo> backups_;
    std::string error_;
    ListCursor cursor_;
};

// What restoring one backup would change, and whether it is allowed.
class RestorePreviewScreen : public Screen {
public:
    RestorePreviewScreen(UiController &ui, app::BackupInfo backup);

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;

private:
    void confirm();

    app::BackupInfo backup_;
    app::RestoreCheck check_;
    std::vector<std::pair<uint32_t, std::string>> paragraphs_; // colour, text
    std::vector<std::pair<uint32_t, std::string>> lines_;      // wrapped on first draw
    size_t scroll_ = 0;
    size_t visibleLines_ = 0;
};

} // namespace ui

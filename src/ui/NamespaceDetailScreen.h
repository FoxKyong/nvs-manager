#pragma once

#include <string>
#include <vector>

#include <esp_err.h>

#include "nvs/NvsTypes.h"
#include "ui/Screen.h"

namespace ui {

// Keys of one namespace with type and a short value: integers in full,
// strings and blobs only as their size.
class NamespaceDetailScreen : public Screen {
public:
    NamespaceDetailScreen(UiController &ui, std::string ns);

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;
    bool textMode() const override { return search_.active; }
    void onModelRefreshed() override { reload(selectedKey()); }

private:
    void reload(const std::string &keepSelected);
    void rebuild(const std::string &keepSelected);
    std::string selectedKey() const;
    int rowsTop() const;
    size_t visibleRows() const;

    std::string ns_;
    bool present_ = false;
    nvsm::NamespaceInfo info_;
    esp_err_t error_ = ESP_OK;
    std::vector<nvsm::KeyInfo> keys_;
    std::vector<std::string> summaries_; // parallel to keys_
    std::vector<size_t> visible_;        // indexes into keys_
    SearchField search_;
    ListCursor cursor_;
};

} // namespace ui

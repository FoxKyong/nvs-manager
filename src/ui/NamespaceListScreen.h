#pragma once

#include <string>
#include <vector>

#include "nvs/NvsTypes.h"
#include "ui/Screen.h"

namespace ui {

// Namespaces holding keys, largest first by default, with filter and sort.
class NamespaceListScreen : public Screen {
public:
    explicit NamespaceListScreen(UiController &ui);

    void draw(lgfx::LovyanGFX &g) override;
    void handle(const Input &in) override;
    bool textMode() const override { return search_.active; }
    void onModelRefreshed() override;

private:
    enum class Sort { BySize, ByName };

    void rebuild(const std::string &keepSelected);
    std::string selectedName() const;
    int rowsTop() const;
    size_t visibleRows() const;

    Sort sort_ = Sort::BySize;
    SearchField search_;
    ListCursor cursor_;
    // A filtered, sorted copy. Never indexes into the model: a change can make
    // namespaces disappear from it before this screen is told.
    std::vector<nvsm::NamespaceInfo> items_;
};

} // namespace ui

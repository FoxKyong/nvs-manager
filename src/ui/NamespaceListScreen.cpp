#include "ui/NamespaceListScreen.h"

#include <algorithm>
#include <cstdio>
#include <memory>

#include "ui/Actions.h"
#include "ui/Draw.h"
#include "ui/HelpScreen.h"
#include "ui/NamespaceDetailScreen.h"
#include "ui/UiController.h"

namespace ui {

NamespaceListScreen::NamespaceListScreen(UiController &ui) : Screen(ui) { rebuild(""); }

void NamespaceListScreen::onModelRefreshed() { rebuild(selectedName()); }

std::string NamespaceListScreen::selectedName() const {
    return cursor_.empty() ? std::string() : items_[cursor_.selected()].name;
}

void NamespaceListScreen::rebuild(const std::string &keepSelected) {
    items_.clear();
    for (const auto &ns : ui_.model().namespaces()) {
        if (search_.matches(ns.name)) items_.push_back(ns);
    }
    // The model lists largest first already.
    if (sort_ == Sort::ByName) {
        std::sort(items_.begin(), items_.end(),
                  [](const nvsm::NamespaceInfo &a, const nvsm::NamespaceInfo &b) { return a.name < b.name; });
    }
    size_t selected = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].name == keepSelected) selected = i;
    }
    cursor_.reset(items_.size(), selected);
    cursor_.follow(visibleRows());
}

int NamespaceListScreen::rowsTop() const { return theme::kHeaderH + (search_.shown() ? theme::kRowH : 0); }

size_t NamespaceListScreen::visibleRows() const {
    const int h = ui_.height() - theme::kFooterH - rowsTop();
    return h > 0 ? static_cast<size_t>(h / theme::kRowH) : 0;
}

void NamespaceListScreen::handle(const Input &in) {
    const size_t rows = visibleRows();
    ui_.invalidate();

    if (search_.active) {
        if (in.nav == Nav::Up || in.nav == Nav::Down) {
            cursor_.move(in.nav == Nav::Up ? -1 : 1, rows);
            return;
        }
        const std::string keep = selectedName();
        if (search_.handle(in)) rebuild(keep);
        cursor_.follow(visibleRows());
        return;
    }

    switch (in.nav) {
    case Nav::Up: cursor_.move(-1, rows); break;
    case Nav::Down: cursor_.move(1, rows); break;
    case Nav::PageUp: cursor_.move(-static_cast<long>(rows), rows); break;
    case Nav::PageDown: cursor_.move(static_cast<long>(rows), rows); break;
    case Nav::Enter:
        if (!cursor_.empty()) ui_.push(std::unique_ptr<Screen>(new NamespaceDetailScreen(ui_, selectedName())));
        break;
    case Nav::Back:
        if (search_.text.empty()) {
            ui_.pop();
        } else {
            const std::string keep = selectedName();
            search_.text.clear();
            rebuild(keep);
        }
        break;
    case Nav::Char:
        switch (in.ch) {
        case '/': search_.active = true; break;
        case 's':
        case 'S': {
            const std::string keep = selectedName();
            sort_ = sort_ == Sort::BySize ? Sort::ByName : Sort::BySize;
            rebuild(keep);
            break;
        }
        case 'd':
        case 'D':
            if (!cursor_.empty()) confirmDeleteNamespace(ui_, items_[cursor_.selected()]);
            break;
        case 'r':
        case 'R': ui_.refreshModel(); break;
        case 'i':
        case 'I': ui_.push(std::unique_ptr<Screen>(new HelpScreen(ui_))); break;
        default: break;
        }
        break;
    default: break;
    }
}

void NamespaceListScreen::draw(lgfx::LovyanGFX &g) {
    const app::NvsModel &m = ui_.model();
    const int w = g.width();
    char buf[64];

    snprintf(buf, sizeof buf, "%u  by %s", static_cast<unsigned>(items_.size()), sort_ == Sort::BySize ? "size" : "name");
    drawHeader(g, "Namespaces", buf);

    int y = theme::kHeaderH;
    if (search_.shown()) y += drawSearchBar(g, y, search_);

    const size_t rows = visibleRows();
    cursor_.follow(rows);

    g.setTextDatum(textdatum_t::middle_left);
    if (m.namespacesError() != ESP_OK) {
        g.setFont(theme::fontSmall());
        g.setTextColor(theme::kHealthFull);
        snprintf(buf, sizeof buf, "Cannot list: %s", esp_err_to_name(m.namespacesError()));
        g.drawString(clipText(g, buf, w - 8).c_str(), 4, y + theme::kRowH / 2 + 1);
    } else if (items_.empty()) {
        g.setFont(theme::fontNormal());
        g.setTextColor(theme::kTextDim);
        g.drawString(m.namespaces().empty() ? "No namespace holds a key." : "No match.", 4, y + theme::kRowH / 2 + 1);
    }

    for (size_t r = 0; r < rows && cursor_.top() + r < items_.size(); ++r) {
        const size_t index = cursor_.top() + r;
        const nvsm::NamespaceInfo &ns = items_[index];
        const int rowY = y + static_cast<int>(r) * theme::kRowH;
        const int midY = rowY + theme::kRowH / 2 + 1;

        if (index == cursor_.selected()) g.fillRect(0, rowY, w, theme::kRowH, theme::kSelection);

        const nvsm::NamespaceClass cls = nvsm::classify(ns.name);
        const int badgeW = drawBadge(g, w - 3, rowY + 1, theme::kRowH - 2, nvsm::classShortName(cls), classColor(cls), true);

        g.setFont(theme::fontNormal());
        g.setTextColor(theme::kText);
        snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(ns.usedEntries));
        const int countRight = w - 3 - badgeW - 6;
        g.setTextDatum(textdatum_t::middle_right);
        g.drawString(buf, countRight, midY);

        g.setTextDatum(textdatum_t::middle_left);
        g.drawString(clipText(g, ns.name, countRight - g.textWidth(buf) - 12).c_str(), 4, midY);
    }

    // Below the last row, say what the list cannot show.
    const size_t shown = items_.size() - cursor_.top();
    if (search_.text.empty() && m.unattributedEntries() > 0 && shown < rows) {
        const int noteY = y + static_cast<int>(shown) * theme::kRowH;
        g.setFont(theme::fontSmall());
        g.setTextColor(theme::kTextDim);
        g.setTextDatum(textdatum_t::middle_left);
        snprintf(buf, sizeof buf, "+%u entries in empty or orphaned namespaces",
                 static_cast<unsigned>(m.unattributedEntries()));
        g.drawString(clipText(g, buf, w - 8).c_str(), 4, noteY + theme::kRowH / 2 + 1);
    }

    drawFooter(g, search_.active ? "type to filter  Enter:done  Esc:clear" : "Enter:open  D:delete  /:find  S:sort");
}

} // namespace ui

#include "ui/NamespaceDetailScreen.h"

#include <cstdio>
#include <memory>

#include "nvs/NvsProtection.h"
#include "ui/Actions.h"
#include "ui/Draw.h"
#include "ui/HelpScreen.h"
#include "ui/KeyDetailScreen.h"
#include "ui/UiController.h"

namespace ui {
namespace {

constexpr int kSubLineH = 12;

} // namespace

NamespaceDetailScreen::NamespaceDetailScreen(UiController &ui, std::string ns) : Screen(ui), ns_(std::move(ns)) {
    reload("");
}

void NamespaceDetailScreen::reload(const std::string &keepSelected) {
    const app::NvsModel &m = ui_.model();
    present_ = false;
    for (const auto &ns : m.namespaces()) {
        if (ns.name == ns_) {
            info_ = ns;
            present_ = true;
        }
    }

    keys_.clear();
    summaries_.clear();
    error_ = ESP_OK;
    if (present_) {
        error_ = m.keys(ns_, keys_);
        for (const auto &k : keys_) {
            if (!isInteger(k.type)) {
                summaries_.push_back(formatBytes(k.dataSize));
                continue;
            }
            nvsm::Value v;
            summaries_.push_back(m.readValue(ns_, k, v) == ESP_OK ? formatInteger(v) : "read error");
        }
    }
    rebuild(keepSelected);
}

void NamespaceDetailScreen::rebuild(const std::string &keepSelected) {
    visible_.clear();
    size_t selected = 0;
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (!search_.matches(keys_[i].name)) continue;
        if (keys_[i].name == keepSelected) selected = visible_.size();
        visible_.push_back(i);
    }
    cursor_.reset(visible_.size(), selected);
    cursor_.follow(visibleRows());
}

std::string NamespaceDetailScreen::selectedKey() const {
    return cursor_.empty() ? std::string() : keys_[visible_[cursor_.selected()]].name;
}

int NamespaceDetailScreen::rowsTop() const {
    return theme::kHeaderH + kSubLineH + (search_.shown() ? theme::kRowH : 0);
}

size_t NamespaceDetailScreen::visibleRows() const {
    const int h = ui_.height() - theme::kFooterH - rowsTop();
    return h > 0 ? static_cast<size_t>(h / theme::kRowH) : 0;
}

void NamespaceDetailScreen::handle(const Input &in) {
    const size_t rows = visibleRows();
    ui_.invalidate();

    if (search_.active) {
        if (in.nav == Nav::Up || in.nav == Nav::Down) {
            cursor_.move(in.nav == Nav::Up ? -1 : 1, rows);
            return;
        }
        const std::string keep = selectedKey();
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
        if (!cursor_.empty()) {
            ui_.push(std::unique_ptr<Screen>(new KeyDetailScreen(ui_, ns_, keys_[visible_[cursor_.selected()]])));
        }
        break;
    case Nav::Back:
        if (search_.text.empty()) {
            ui_.pop();
        } else {
            const std::string keep = selectedKey();
            search_.text.clear();
            rebuild(keep);
        }
        break;
    case Nav::Char:
        switch (in.ch) {
        case '/': search_.active = true; break;
        case 'e':
        case 'E':
            if (!cursor_.empty()) startEdit(ui_, ns_, keys_[visible_[cursor_.selected()]]);
            break;
        case 'd':
        case 'D':
            if (!cursor_.empty()) confirmDeleteKey(ui_, ns_, keys_[visible_[cursor_.selected()]], 0);
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

void NamespaceDetailScreen::draw(lgfx::LovyanGFX &g) {
    const int w = g.width();
    char buf[64];

    const nvsm::NamespaceClass cls = nvsm::classify(ns_);
    g.setFont(theme::fontSmall());
    const int badgeW = g.textWidth(nvsm::className(cls)) + 6;
    drawHeader(g, ns_, nullptr, badgeW + 4);
    drawBadge(g, w - 3, 2, theme::kHeaderH - 4, nvsm::className(cls), classColor(cls), true);

    int y = theme::kHeaderH;
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::kTextDim);
    g.setTextDatum(textdatum_t::middle_left);
    const char *owner = nvsm::knownOwner(ns_);
    if (present_) {
        snprintf(buf, sizeof buf, "%u entries, %u %s%s%s", static_cast<unsigned>(info_.usedEntries),
                 static_cast<unsigned>(info_.keyCount), info_.keyCount == 1 ? "key" : "keys", owner ? "  |  " : "",
                 owner ? owner : "");
    } else {
        snprintf(buf, sizeof buf, "no keys%s%s", owner ? "  |  " : "", owner ? owner : "");
    }
    g.drawString(clipText(g, buf, w - 8).c_str(), 4, y + kSubLineH / 2);
    y += kSubLineH;

    if (search_.shown()) y += drawSearchBar(g, y, search_);

    const size_t rows = visibleRows();
    cursor_.follow(rows);

    if (!present_ || error_ != ESP_OK || visible_.empty()) {
        g.setFont(theme::fontNormal());
        g.setTextColor(error_ == ESP_OK ? theme::kTextDim : theme::kHealthFull);
        if (!present_) {
            // It vanished from the list because it holds no key; its own
            // entry stays, which the dashboard counts as an empty namespace.
            snprintf(buf, sizeof buf, "No keys left.");
        } else if (error_ != ESP_OK) {
            snprintf(buf, sizeof buf, "Cannot list: %s", esp_err_to_name(error_));
        } else {
            snprintf(buf, sizeof buf, "%s", keys_.empty() ? "No keys." : "No match.");
        }
        g.drawString(clipText(g, buf, w - 8).c_str(), 4, y + theme::kRowH / 2 + 1);
    }

    // Key names are up to 15 characters; values in this list stay short.
    constexpr int kTypeX = 124;
    for (size_t r = 0; r < rows && cursor_.top() + r < visible_.size(); ++r) {
        const size_t index = cursor_.top() + r;
        const nvsm::KeyInfo &key = keys_[visible_[index]];
        const int rowY = y + static_cast<int>(r) * theme::kRowH;
        const int midY = rowY + theme::kRowH / 2 + 1;

        if (index == cursor_.selected()) g.fillRect(0, rowY, w, theme::kRowH, theme::kSelection);

        g.setFont(theme::fontNormal());
        g.setTextColor(theme::kText);
        g.setTextDatum(textdatum_t::middle_left);
        g.drawString(clipText(g, key.name, kTypeX - 8).c_str(), 4, midY);

        g.setFont(theme::fontSmall());
        g.setTextColor(theme::kTextDim);
        g.drawString(nvsm::typeName(key.type), kTypeX, midY);

        g.setFont(theme::fontNormal());
        g.setTextColor(theme::kText);
        g.setTextDatum(textdatum_t::middle_right);
        g.drawString(clipText(g, summaries_[visible_[index]], w - kTypeX - 34).c_str(), w - 4, midY);
    }

    drawFooter(g, search_.active ? "type to filter  Enter:done  Esc:clear" : "Enter:view  E:edit  D:delete  /:find");
}

} // namespace ui

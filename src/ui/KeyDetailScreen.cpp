#include "ui/KeyDetailScreen.h"

#include <algorithm>
#include <cstdio>

#include "ui/Actions.h"
#include "ui/Draw.h"
#include "ui/UiController.h"

namespace ui {
namespace {

constexpr int kLabelX = 4;
constexpr int kValueX = 76; // clear of the widest label, "Namespace"
constexpr size_t kHexBytesPerLine = 8;

} // namespace

KeyDetailScreen::KeyDetailScreen(UiController &ui, std::string ns, nvsm::KeyInfo key)
    : Screen(ui), ns_(std::move(ns)), key_(std::move(key)) {
    load();
}

void KeyDetailScreen::load() {
    error_ = ui_.model().readValue(ns_, key_, value_);
    scroll_ = 0;
}

std::vector<std::string> KeyDetailScreen::valueLines(lgfx::LovyanGFX &g, int width) const {
    std::vector<std::string> lines;
    if (key_.type == nvsm::ValueType::Str) {
        // Wrap by pixel width; unprintable bytes are shown as dots.
        std::string line;
        for (char c : value_.str) {
            const char shown = (c >= 0x20 && c <= 0x7E) ? c : '.';
            if (g.textWidth((line + shown).c_str()) > width) {
                lines.push_back(line);
                line.clear();
            }
            line += shown;
        }
        if (!line.empty() || lines.empty()) lines.push_back(line.empty() ? "(empty)" : line);
    } else if (key_.type == nvsm::ValueType::Blob) {
        char buf[64];
        for (size_t off = 0; off < value_.blob.size(); off += kHexBytesPerLine) {
            int n = snprintf(buf, sizeof buf, "%04X ", static_cast<unsigned>(off));
            for (size_t i = off; i < off + kHexBytesPerLine && i < value_.blob.size(); ++i) {
                n += snprintf(buf + n, sizeof buf - n, " %02X", value_.blob[i]);
            }
            lines.push_back(buf);
        }
        if (lines.empty()) lines.push_back("(empty)");
    }
    return lines;
}

void KeyDetailScreen::draw(lgfx::LovyanGFX &g) {
    const int w = g.width();
    char buf[64];

    drawHeader(g, key_.name, nvsm::typeName(key_.type));

    g.setFont(theme::fontSmall());
    g.setTextDatum(textdatum_t::top_left);
    int y = theme::kHeaderH + 3;
    const auto row = [&](const char *label, const std::string &value, uint32_t color) {
        g.setTextColor(theme::kTextDim);
        g.drawString(label, kLabelX, y);
        g.setTextColor(color);
        g.drawString(clipText(g, value, w - kValueX - 4).c_str(), kValueX, y);
        y += theme::kSmallRowH;
    };

    row("Namespace", ns_, theme::kText);
    const size_t bytes = isInteger(key_.type) ? integerWidth(key_.type) : key_.dataSize;
    snprintf(buf, sizeof buf, "%s, ~%u %s", formatBytes(bytes).c_str(), static_cast<unsigned>(key_.entriesEstimate),
             key_.entriesEstimate == 1 ? "entry" : "entries");
    row("Size", buf, theme::kText);

    const bool hideable = !isInteger(key_.type);
    if (error_ != ESP_OK) {
        row("Value", std::string("cannot read: ") + esp_err_to_name(error_), theme::kHealthFull);
    } else if (!hideable) {
        row("Value", formatInteger(value_), theme::kText);
    } else if (!revealed_) {
        row("Value", "hidden, V to show", theme::kTextDim);
    } else {
        g.setTextColor(theme::kTextDim);
        g.drawString("Value", kLabelX, y);
        y += theme::kSmallRowH;

        const int footerTop = g.height() - theme::kFooterH;
        const std::vector<std::string> lines = valueLines(g, w - 8);
        const size_t visible = static_cast<size_t>((footerTop - y) / theme::kSmallRowH);
        lastLineCount_ = lines.size();
        lastVisibleLines_ = visible;
        if (scroll_ + visible > lines.size()) scroll_ = lines.size() > visible ? lines.size() - visible : 0;

        g.setTextColor(theme::kText);
        for (size_t i = 0; i < visible && scroll_ + i < lines.size(); ++i) {
            g.drawString(lines[scroll_ + i].c_str(), kLabelX, y);
            y += theme::kSmallRowH;
        }
        if (lines.size() > visible) {
            g.setTextColor(theme::kTextDim);
            g.setTextDatum(textdatum_t::top_right);
            snprintf(buf, sizeof buf, "%u-%u/%u", static_cast<unsigned>(scroll_ + 1),
                     static_cast<unsigned>(std::min(scroll_ + visible, lines.size())), static_cast<unsigned>(lines.size()));
            g.drawString(buf, w - 4, theme::kHeaderH + 3 + 2 * theme::kSmallRowH);
        }
    }

    if (!hideable || error_ != ESP_OK) {
        drawFooter(g, "E:edit  D:delete  Esc:back");
    } else if (key_.type == nvsm::ValueType::Blob) {
        drawFooter(g, revealed_ ? "V:hide  ; .:scroll  D:delete" : "V:show  D:delete  Esc:back");
    } else {
        drawFooter(g, revealed_ ? "V:hide  ; .:scroll  E:edit  D:del" : "V:show  E:edit  D:delete  Esc:back");
    }
}

void KeyDetailScreen::handle(const Input &in) {
    ui_.invalidate();
    switch (in.nav) {
    case Nav::Back: ui_.pop(); break;
    case Nav::Up:
        if (scroll_ > 0) --scroll_;
        break;
    case Nav::Down:
        if (scroll_ + lastVisibleLines_ < lastLineCount_) ++scroll_;
        break;
    case Nav::Char:
        switch (in.ch) {
        case 'v':
        case 'V':
            if (!isInteger(key_.type)) {
                revealed_ = !revealed_;
                scroll_ = 0;
            }
            break;
        case 'e':
        case 'E': startEdit(ui_, ns_, key_); break;
        case 'd':
        case 'D': confirmDeleteKey(ui_, ns_, key_, 1); break;
        default: break;
        }
        break;
    default: break;
    }
}

} // namespace ui

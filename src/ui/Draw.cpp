#include "ui/Draw.h"

#include <cinttypes>
#include <cstdio>

namespace ui {

void drawHeader(lgfx::LovyanGFX &g, const std::string &title, const char *right, int reserveRight) {
    const int w = g.width();
    const int midY = theme::kHeaderH / 2 + 1;
    g.fillRect(0, 0, w, theme::kHeaderH, theme::kHeader);
    g.setFont(theme::fontNormal());

    int rightWidth = reserveRight;
    if (right != nullptr) {
        g.setTextDatum(textdatum_t::middle_right);
        g.setTextColor(theme::kText);
        g.drawString(right, w - 4 - reserveRight, midY);
        rightWidth += g.textWidth(right) + 8;
    }
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(theme::kText);
    g.drawString(clipText(g, title, w - 8 - rightWidth).c_str(), 4, midY);
}

void drawFooter(lgfx::LovyanGFX &g, const char *help) {
    const int y = g.height() - theme::kFooterH;
    g.fillRect(0, y, g.width(), theme::kFooterH, theme::kFooter);
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::kTextDim);
    g.setTextDatum(textdatum_t::middle_left);
    g.drawString(clipText(g, help, g.width() - 8).c_str(), 4, y + theme::kFooterH / 2 + 1);
}

int drawBadge(lgfx::LovyanGFX &g, int x, int top, int height, const char *text, uint32_t fill, bool alignRight) {
    g.setFont(theme::fontSmall());
    const int w = g.textWidth(text) + 6;
    const int left = alignRight ? x - w : x;
    g.fillRoundRect(left, top, w, height, 2, fill);
    g.setTextColor(theme::kText);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString(text, left + w / 2, top + height / 2 + 1);
    return w;
}

int drawSearchBar(lgfx::LovyanGFX &g, int y, const SearchField &search) {
    g.fillRect(0, y, g.width(), theme::kRowH, search.active ? theme::kSearchActive : theme::kSearchIdle);
    g.setFont(theme::fontNormal());
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(search.active ? theme::kText : theme::kTextDim);
    const std::string text = "Find: " + search.text + (search.active ? "_" : "");
    g.drawString(clipText(g, text, g.width() - 8).c_str(), 4, y + theme::kRowH / 2 + 1);
    return theme::kRowH;
}

std::string clipText(lgfx::LovyanGFX &g, const std::string &text, int maxWidth) {
    if (maxWidth <= 0) return {};
    if (g.textWidth(text.c_str()) <= maxWidth) return text;
    const int dots = g.textWidth("..");
    std::string s = text;
    while (!s.empty() && g.textWidth(s.c_str()) + dots > maxWidth) s.pop_back();
    return s + "..";
}

uint32_t classColor(nvsm::NamespaceClass cls) {
    switch (cls) {
    case nvsm::NamespaceClass::System: return theme::kClassSystem;
    case nvsm::NamespaceClass::Launcher: return theme::kClassLauncher;
    case nvsm::NamespaceClass::Application: return theme::kClassApp;
    }
    return theme::kClassApp;
}

uint32_t healthColor(app::Health health) {
    switch (health) {
    case app::Health::Ok: return theme::kHealthOk;
    case app::Health::Low: return theme::kHealthLow;
    case app::Health::Critical: return theme::kHealthCritical;
    case app::Health::Full: return theme::kHealthFull;
    }
    return theme::kHealthFull;
}

std::string formatBytes(size_t bytes) {
    char buf[24];
    if (bytes < 1024) {
        snprintf(buf, sizeof buf, "%u B", static_cast<unsigned>(bytes));
    } else {
        snprintf(buf, sizeof buf, "%.1f KiB", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

bool isInteger(nvsm::ValueType type) {
    return type != nvsm::ValueType::Str && type != nvsm::ValueType::Blob && type != nvsm::ValueType::Unknown;
}

size_t integerWidth(nvsm::ValueType type) {
    switch (type) {
    case nvsm::ValueType::U8:
    case nvsm::ValueType::I8: return 1;
    case nvsm::ValueType::U16:
    case nvsm::ValueType::I16: return 2;
    case nvsm::ValueType::U32:
    case nvsm::ValueType::I32: return 4;
    case nvsm::ValueType::U64:
    case nvsm::ValueType::I64: return 8;
    default: return 0;
    }
}

std::vector<std::string> wrapText(lgfx::LovyanGFX &g, const std::string &text, int width) {
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    const auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && g.textWidth(candidate.c_str()) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
        // A single word wider than the line (e.g. a file path) is split
        // wherever it has to be.
        while (g.textWidth(line.c_str()) > width && line.size() > 1) {
            size_t fit = line.size() - 1;
            while (fit > 1 && g.textWidth(line.substr(0, fit).c_str()) > width) --fit;
            lines.push_back(line.substr(0, fit));
            line.erase(0, fit);
        }
    };
    for (char c : text) {
        if (c == ' ') {
            flushWord();
        } else {
            word += c;
        }
    }
    flushWord();
    lines.push_back(line);
    return lines;
}

bool isSignedInteger(nvsm::ValueType type) {
    return type == nvsm::ValueType::I8 || type == nvsm::ValueType::I16 || type == nvsm::ValueType::I32 ||
           type == nvsm::ValueType::I64;
}

std::string formatDecimal(const nvsm::Value &value) {
    char buf[32];
    if (isSignedInteger(value.type)) {
        snprintf(buf, sizeof buf, "%" PRId64, value.i);
    } else if (isInteger(value.type)) {
        snprintf(buf, sizeof buf, "%" PRIu64, value.u);
    } else {
        return "?";
    }
    return buf;
}

std::string formatInteger(const nvsm::Value &value) {
    char buf[48];
    switch (value.type) {
    case nvsm::ValueType::I8:
    case nvsm::ValueType::I16:
    case nvsm::ValueType::I32:
    case nvsm::ValueType::I64: snprintf(buf, sizeof buf, "%" PRId64, value.i); break;
    case nvsm::ValueType::U8:
    case nvsm::ValueType::U16:
    case nvsm::ValueType::U32:
    case nvsm::ValueType::U64:
        if (value.u < 10) {
            snprintf(buf, sizeof buf, "%" PRIu64, value.u);
        } else {
            snprintf(buf, sizeof buf, "%" PRIu64 " (0x%" PRIX64 ")", value.u, value.u);
        }
        break;
    default: return "?";
    }
    return buf;
}

} // namespace ui

#include "ui/EditValueScreen.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "app/Config.h"
#include "ui/Actions.h"
#include "ui/Draw.h"
#include "ui/UiController.h"

namespace ui {
namespace {

constexpr int kValueX = 76;
constexpr size_t kMaxNumberLength = 24;

const char *rangeText(nvsm::ValueType type) {
    switch (type) {
    case nvsm::ValueType::U8: return "0 to 255";
    case nvsm::ValueType::I8: return "-128 to 127";
    case nvsm::ValueType::U16: return "0 to 65535";
    case nvsm::ValueType::I16: return "-32768 to 32767";
    case nvsm::ValueType::U32: return "0 to 4294967295";
    case nvsm::ValueType::I32: return "-2147483648 to 2147483647";
    case nvsm::ValueType::U64: return "unsigned 64-bit";
    case nvsm::ValueType::I64: return "signed 64-bit";
    case nvsm::ValueType::Str: return "text, up to 255 characters";
    default: return "not editable";
    }
}

// Decimal, or hexadecimal with 0x; an optional sign. No octal surprises:
// "010" is ten.
bool parseNumber(const std::string &text, bool allowNegative, bool &negative, unsigned long long &magnitude) {
    size_t pos = 0;
    negative = false;
    if (!text.empty() && (text[0] == '-' || text[0] == '+')) {
        negative = text[0] == '-';
        pos = 1;
    }
    int base = 10;
    if (text.size() > pos + 1 && text[pos] == '0' && (text[pos + 1] == 'x' || text[pos + 1] == 'X')) {
        base = 16;
        pos += 2;
    }
    if (pos >= text.size()) return false;
    for (size_t i = pos; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (base == 10 ? !std::isdigit(c) : !std::isxdigit(c)) return false;
    }
    errno = 0;
    magnitude = std::strtoull(text.c_str() + pos, nullptr, base);
    if (errno == ERANGE) return false;
    return allowNegative || !negative || magnitude == 0;
}

} // namespace

EditValueScreen::EditValueScreen(UiController &ui, std::string ns, nvsm::KeyInfo key)
    : Screen(ui), ns_(std::move(ns)), key_(std::move(key)) {
    readError_ = ui_.model().readValue(ns_, key_, current_);
    // Start from the current value, so a small change does not mean retyping it all.
    if (readError_ == ESP_OK) {
        text_ = key_.type == nvsm::ValueType::Str ? current_.str.substr(0, maxLength()) : formatDecimal(current_);
    }
}

std::string EditValueScreen::currentText() const {
    if (readError_ != ESP_OK) return std::string("unreadable: ") + esp_err_to_name(readError_);
    if (key_.type == nvsm::ValueType::Str) {
        char buf[40];
        snprintf(buf, sizeof buf, "%u characters", static_cast<unsigned>(current_.str.size()));
        return buf;
    }
    return formatDecimal(current_);
}

size_t EditValueScreen::maxLength() const {
    return key_.type == nvsm::ValueType::Str ? app::config::kMaxEditStringLength : kMaxNumberLength;
}

bool EditValueScreen::parse(nvsm::Value &out) {
    out = nvsm::Value{};
    out.type = key_.type;
    if (key_.type == nvsm::ValueType::Str) {
        out.str = text_;
        return true;
    }

    const bool isSigned = isSignedInteger(key_.type);
    bool negative = false;
    unsigned long long magnitude = 0;
    if (!parseNumber(text_, isSigned, negative, magnitude)) {
        problem_ = text_.empty() ? "Type a number." : std::string("Not a valid number for ") + nvsm::typeName(key_.type);
        return false;
    }

    if (isSigned) {
        long long lo = LLONG_MIN;
        long long hi = LLONG_MAX;
        switch (key_.type) {
        case nvsm::ValueType::I8: lo = INT8_MIN, hi = INT8_MAX; break;
        case nvsm::ValueType::I16: lo = INT16_MIN, hi = INT16_MAX; break;
        case nvsm::ValueType::I32: lo = INT32_MIN, hi = INT32_MAX; break;
        default: break;
        }
        const unsigned long long limit = negative ? static_cast<unsigned long long>(-(lo + 1)) + 1 : hi;
        if (magnitude > limit) {
            problem_ = std::string("Out of range: ") + rangeText(key_.type);
            return false;
        }
        out.i = negative ? (magnitude == 0 ? 0 : -static_cast<long long>(magnitude - 1) - 1)
                         : static_cast<long long>(magnitude);
    } else {
        unsigned long long hi = ULLONG_MAX;
        switch (key_.type) {
        case nvsm::ValueType::U8: hi = UINT8_MAX; break;
        case nvsm::ValueType::U16: hi = UINT16_MAX; break;
        case nvsm::ValueType::U32: hi = UINT32_MAX; break;
        default: break;
        }
        if (magnitude > hi) {
            problem_ = std::string("Out of range: ") + rangeText(key_.type);
            return false;
        }
        out.u = magnitude;
    }
    return true;
}

void EditValueScreen::handle(const Input &in) {
    ui_.invalidate();
    switch (in.nav) {
    case Nav::Back: ui_.pop(); break;
    case Nav::Erase:
        if (!text_.empty()) text_.pop_back();
        problem_.clear();
        break;
    case Nav::Char:
        if (text_.size() < maxLength()) text_ += in.ch;
        problem_.clear();
        break;
    case Nav::Enter: {
        nvsm::Value value;
        if (parse(value)) confirmEdit(ui_, ns_, key_, currentText(), value);
        break;
    }
    default: break;
    }
}

void EditValueScreen::draw(lgfx::LovyanGFX &g) {
    const int w = g.width();
    drawHeader(g, "Edit " + key_.name, nvsm::typeName(key_.type));

    g.setFont(theme::fontSmall());
    g.setTextDatum(textdatum_t::top_left);
    int y = theme::kHeaderH + 3;
    const auto row = [&](const char *label, const std::string &value, uint32_t color) {
        g.setTextColor(theme::kTextDim);
        g.drawString(label, 4, y);
        g.setTextColor(color);
        g.drawString(clipText(g, value, w - kValueX - 4).c_str(), kValueX, y);
        y += theme::kSmallRowH;
    };
    row("Namespace", ns_, theme::kText);
    row("Current", currentText(), readError_ == ESP_OK ? theme::kText : theme::kHealthFull);
    row("Allowed", rangeText(key_.type), theme::kTextDim);

    y += 3;
    constexpr int kBoxH = 17;
    g.fillRect(2, y, w - 4, kBoxH, theme::kSearchActive);
    g.setFont(theme::fontNormal());
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(theme::kText);
    std::string shown = text_ + "_";
    while (shown.size() > 1 && g.textWidth(shown.c_str()) > w - 12) shown.erase(0, 1); // keep the end visible
    g.drawString(shown.c_str(), 6, y + kBoxH / 2 + 1);
    y += kBoxH + 3;

    if (!problem_.empty()) {
        g.setFont(theme::fontSmall());
        g.setTextDatum(textdatum_t::top_left);
        g.setTextColor(theme::kHealthFull);
        g.drawString(clipText(g, problem_, w - 8).c_str(), 4, y);
    }
    drawFooter(g, "Enter:review  Del:erase  Esc:cancel");
}

} // namespace ui

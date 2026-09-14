#include "ui/Input.h"

#include <algorithm>
#include <cctype>

namespace ui {

Input translate(const platform::KeyEvent &ev, bool textMode) {
    using platform::Key;
    Input in;
    switch (ev.key) {
    case Key::Up: in.nav = Nav::Up; return in;
    case Key::Down: in.nav = Nav::Down; return in;
    case Key::Left: in.nav = Nav::PageUp; return in;
    case Key::Right: in.nav = Nav::PageDown; return in;
    case Key::Enter: in.nav = Nav::Enter; return in;
    case Key::Escape: in.nav = Nav::Back; return in;
    case Key::Backspace: in.nav = textMode ? Nav::Erase : Nav::Back; return in;
    case Key::Tab:
    case Key::None: return in;
    case Key::Char: break;
    }

    if (!textMode) {
        switch (ev.ch) {
        case ';': in.nav = Nav::Up; return in;
        case '.': in.nav = Nav::Down; return in;
        case ',': in.nav = Nav::PageUp; return in;
        case '`': in.nav = Nav::Back; return in;
        default: break;
        }
    }
    in.nav = Nav::Char;
    in.ch = ev.ch;
    return in;
}

bool SearchField::matches(const std::string &s) const {
    if (text.empty()) return true;
    const auto lower = [](unsigned char c) { return std::tolower(c); };
    return std::search(s.begin(), s.end(), text.begin(), text.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != s.end();
}

bool SearchField::handle(const Input &in) {
    switch (in.nav) {
    case Nav::Char:
        if (text.size() >= kMaxLength) return false;
        text += in.ch;
        return true;
    case Nav::Erase:
        if (text.empty()) return false;
        text.pop_back();
        return true;
    case Nav::Enter: active = false; return false;
    case Nav::Back:
        active = false;
        if (text.empty()) return false;
        text.clear();
        return true;
    default: return false;
    }
}

void ListCursor::reset(size_t count, size_t selected) {
    count_ = count;
    selected_ = count == 0 ? 0 : std::min(selected, count - 1);
    top_ = std::min(top_, selected_);
}

void ListCursor::move(long delta, size_t visibleRows) {
    if (count_ == 0) return;
    long next = static_cast<long>(selected_) + delta;
    next = std::max(0L, std::min(next, static_cast<long>(count_) - 1));
    selected_ = static_cast<size_t>(next);
    follow(visibleRows);
}

void ListCursor::follow(size_t visibleRows) {
    if (visibleRows == 0 || count_ <= visibleRows) {
        top_ = 0;
        return;
    }
    if (selected_ < top_) top_ = selected_;
    if (selected_ >= top_ + visibleRows) top_ = selected_ + 1 - visibleRows;
    top_ = std::min(top_, count_ - visibleRows);
}

} // namespace ui

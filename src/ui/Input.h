#pragma once

#include <cstdint>
#include <string>

#include "platform/Platform.h"

namespace ui {

enum class Nav : uint8_t { None, Up, Down, PageUp, PageDown, Enter, Back, Erase, Char };

struct Input {
    Nav nav = Nav::None;
    char ch = 0; // for Nav::Char
};

// Navigation mode: arrows (the fn layer on the Cardputer) and the unshifted
// arrow legends ; . , move, Esc / Del / ` go back, other printable keys are
// commands. Text mode: printable keys are text, Del erases, Esc leaves.
Input translate(const platform::KeyEvent &ev, bool textMode);

// Incremental filter typed by the user; case-insensitive substring match.
struct SearchField {
    static constexpr size_t kMaxLength = 15; // longest NVS key or namespace name

    bool active = false;
    std::string text;

    bool shown() const { return active || !text.empty(); }
    bool matches(const std::string &s) const;

    // Handles a text-mode input; returns true when the filter text changed.
    bool handle(const Input &in);
};

// Selection and scroll position of a list with `count` rows.
class ListCursor {
public:
    void reset(size_t count, size_t selected);
    void move(long delta, size_t visibleRows);
    // Scrolls so the selection is inside `visibleRows` rows.
    void follow(size_t visibleRows);

    bool empty() const { return count_ == 0; }
    size_t count() const { return count_; }
    size_t selected() const { return selected_; }
    size_t top() const { return top_; }

private:
    size_t count_ = 0;
    size_t selected_ = 0;
    size_t top_ = 0;
};

} // namespace ui

#pragma once

#include <string>
#include <vector>

#include "app/NvsModel.h"
#include "nvs/NvsProtection.h"
#include "nvs/NvsTypes.h"
#include "ui/Input.h"
#include "ui/Theme.h"

// Drawing helpers and text formatting shared by the screens.
namespace ui {

// Title bar. `reserveRight` keeps room for something drawn over the right end.
void drawHeader(lgfx::LovyanGFX &g, const std::string &title, const char *right = nullptr, int reserveRight = 0);
void drawFooter(lgfx::LovyanGFX &g, const char *help);

// Filled rounded label; `x` is its left edge, or its right edge with
// `alignRight`. Returns the width.
int drawBadge(lgfx::LovyanGFX &g, int x, int top, int height, const char *text, uint32_t fill, bool alignRight);

int drawSearchBar(lgfx::LovyanGFX &g, int y, const SearchField &search); // returns its height

// `text` cut to `maxWidth` in the current font, ending in ".." when cut.
std::string clipText(lgfx::LovyanGFX &g, const std::string &text, int maxWidth);

// `text` broken into lines of at most `width` pixels in the current font, at spaces.
std::vector<std::string> wrapText(lgfx::LovyanGFX &g, const std::string &text, int width);

uint32_t classColor(nvsm::NamespaceClass cls);
uint32_t healthColor(app::Health health);

std::string formatBytes(size_t bytes);
std::string formatInteger(const nvsm::Value &value); // decimal, unsigned ones also in hex
std::string formatDecimal(const nvsm::Value &value); // decimal only
bool isInteger(nvsm::ValueType type);
bool isSignedInteger(nvsm::ValueType type);
size_t integerWidth(nvsm::ValueType type); // bytes

} // namespace ui

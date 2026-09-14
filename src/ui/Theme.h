#pragma once

#include <cstdint>
#include <cstdio> // M5GFX's DataWrapper.hpp uses fopen() without including it

#include <M5GFX.h>

// Colours (RGB888), fonts and fixed metrics shared by every screen, sized for
// a 240x135 display.
namespace ui::theme {

constexpr uint32_t kBackground = 0x101418;
constexpr uint32_t kHeader = 0x006E8C;
constexpr uint32_t kFooter = 0x282E34;
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kTextDim = 0x8C96A0;
constexpr uint32_t kSelection = 0x24435A;
constexpr uint32_t kSearchActive = 0x1F3A4A;
constexpr uint32_t kSearchIdle = 0x1A2026;
constexpr uint32_t kBarTrack = 0x2A3036;

constexpr uint32_t kHealthOk = 0x2E9E5B;
constexpr uint32_t kHealthLow = 0xD99A1E;
constexpr uint32_t kHealthCritical = 0xE0602A;
constexpr uint32_t kHealthFull = 0xD03A3A;

constexpr uint32_t kClassSystem = 0xC0392B;
constexpr uint32_t kClassLauncher = 0xB7791A;
constexpr uint32_t kClassApp = 0x4A6F8F;

constexpr int kHeaderH = 16;
constexpr int kFooterH = 13;
constexpr int kRowH = 13;
constexpr int kSmallRowH = 11;

inline const lgfx::IFont *fontSmall() { return &fonts::DejaVu9; }
inline const lgfx::IFont *fontNormal() { return &fonts::DejaVu12; }
inline const lgfx::IFont *fontLarge() { return &fonts::DejaVu24; }

} // namespace ui::theme

#pragma once

#include <cstddef>
#include <cstdint>

// Tunables of the application, in one place.
namespace app::config {

// Dashboard status, as the share of *usable* entries still available:
//   > kLowPercent        OK
//   kCriticalPercent..kLowPercent   LOW
//   below kCriticalPercent          CRITICAL
//   no available entry              FULL
constexpr int kLowPercent = 25;
constexpr int kCriticalPercent = 10;

// NVS always keeps one page (126 entries) free for garbage collection, so it
// can never be used for data (ESP-IDF PageManager::fillStats).
constexpr size_t kReservedEntries = 126;

// How long Enter must be held to confirm a risky change.
constexpr uint32_t kHoldConfirmMs = 2000;

// Advanced mode locks itself after this long without any key press.
constexpr uint32_t kAdvancedIdleLockMs = 5 * 60 * 1000;

// Longest text accepted when editing a string value.
constexpr size_t kMaxEditStringLength = 255;

} // namespace app::config

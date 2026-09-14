#pragma once

#include <string>

#include "nvs/NvsTypes.h"

// The flows that change NVS: check permission, confirm, run, show the result.
namespace ui {

class UiController;

void showNotAllowed(UiController &ui, const std::string &reason);

void confirmDeleteNamespace(UiController &ui, const nvsm::NamespaceInfo &ns);

// `popsAfter`: screens under the confirmation to close as well once the key
// is gone, e.g. the key's own detail screen.
void confirmDeleteKey(UiController &ui, const std::string &ns, const nvsm::KeyInfo &key, int popsAfter);

void startEdit(UiController &ui, const std::string &ns, const nvsm::KeyInfo &key);
void confirmEdit(UiController &ui, const std::string &ns, const nvsm::KeyInfo &key, const std::string &currentText,
                 const nvsm::Value &newValue);

// Locks Advanced mode at once, or asks (held Enter) before unlocking it.
void toggleAdvancedMode(UiController &ui);

// Asks, then leaves NVS Manager by restarting the device.
void confirmExit(UiController &ui);

} // namespace ui

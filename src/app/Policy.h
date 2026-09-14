#pragma once

#include "nvs/NvsProtection.h"
#include "nvs/NvsTypes.h"

// Who may change what, and how firmly the user has to confirm it.
namespace app {

enum class Action { EditKey, DeleteKey, DeleteNamespace };

enum class Confirmation {
    Press, // Enter
    Hold,  // Enter held for config::kHoldConfirmMs
};

struct Permission {
    bool allowed = false;
    Confirmation confirmation = Confirmation::Hold;
    const char *warning = nullptr; // shown before confirming; may be null
    const char *denied = nullptr;  // why not, when !allowed
};

Permission permissionFor(Action action, nvsm::NamespaceClass cls, bool advancedMode);

// Integers and strings can be edited; blobs are read-only in this version.
bool editable(nvsm::ValueType type);

} // namespace app

#include "nvs/NvsProtection.h"

#include <cstring>

namespace nvsm {
namespace {

struct KnownNamespace {
    const char *name;
    NamespaceClass cls;
    const char *owner;
};

// Sources: ESP-IDF, and Launcher's src/ (docs/RESEARCH.md, section 4).
// Deleting a LAUNCHER namespace can break other firmware, e.g. nimble_bond and
// l_bonds hold BLE pairings that Launcher swaps between firmware.
constexpr KnownNamespace kKnown[] = {
    {"nvs.net80211", NamespaceClass::System, "ESP-IDF Wi-Fi"},
    {"phy", NamespaceClass::System, "ESP-IDF PHY calibration"},
    {"launcher", NamespaceClass::Launcher, "Launcher settings"},
    {"l_wifi", NamespaceClass::Launcher, "Launcher Wi-Fi networks"},
    {"l_apps", NamespaceClass::Launcher, "Launcher app registry"},
    {"l_bonds", NamespaceClass::Launcher, "Launcher BLE bond stash"},
    {"nimble_bond", NamespaceClass::Launcher, "NimBLE bonds, swapped by Launcher"},
    {"uiflow", NamespaceClass::Launcher, "UIFlow2, edited by Launcher"},
    {"touch_cal", NamespaceClass::Launcher, "Launcher touch calibration"},
};

const KnownNamespace *find(const std::string &ns) {
    for (const auto &k : kKnown) {
        if (ns == k.name) return &k;
    }
    return nullptr;
}

} // namespace

NamespaceClass classify(const std::string &ns) {
    const KnownNamespace *k = find(ns);
    return k != nullptr ? k->cls : NamespaceClass::Application;
}

const char *className(NamespaceClass cls) {
    switch (cls) {
    case NamespaceClass::Application: return "APP";
    case NamespaceClass::Launcher: return "LAUNCHER";
    case NamespaceClass::System: return "SYSTEM";
    }
    return "?";
}

const char *classShortName(NamespaceClass cls) {
    switch (cls) {
    case NamespaceClass::Application: return "APP";
    case NamespaceClass::Launcher: return "LNCH";
    case NamespaceClass::System: return "SYS";
    }
    return "?";
}

const char *knownOwner(const std::string &ns) {
    const KnownNamespace *k = find(ns);
    return k != nullptr ? k->owner : nullptr;
}

} // namespace nvsm

#include "app/Policy.h"

namespace app {

Permission permissionFor(Action action, nvsm::NamespaceClass cls, bool advancedMode) {
    Permission p;
    switch (cls) {
    case nvsm::NamespaceClass::System:
        if (!advancedMode) {
            p.denied = "SYSTEM namespace, read-only. Unlock Advanced mode with A on the dashboard first.";
            return p;
        }
        p.allowed = true;
        p.confirmation = Confirmation::Hold;
        p.warning = "SYSTEM: written by ESP-IDF. A change can break Wi-Fi or radio calibration for every firmware.";
        return p;

    case nvsm::NamespaceClass::Launcher:
        p.allowed = true;
        p.confirmation = Confirmation::Hold;
        p.warning = "Used by Launcher or shared between firmware. They may lose settings or BLE pairings.";
        return p;

    case nvsm::NamespaceClass::Application:
        p.allowed = true;
        if (action == Action::DeleteNamespace) {
            p.confirmation = Confirmation::Hold;
            p.warning = "The application that owns it loses all its settings.";
        } else {
            p.confirmation = Confirmation::Press;
        }
        return p;
    }
    return p;
}

bool editable(nvsm::ValueType type) { return type != nvsm::ValueType::Blob && type != nvsm::ValueType::Unknown; }

} // namespace app

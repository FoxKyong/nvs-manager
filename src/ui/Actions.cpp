#include "ui/Actions.h"

#include <cstdio>
#include <memory>

#include "app/Policy.h"
#include "nvs/NvsProtection.h"
#include "platform/Platform.h"
#include "ui/ConfirmScreen.h"
#include "ui/Draw.h"
#include "ui/EditValueScreen.h"
#include "ui/ResultScreen.h"
#include "ui/UiController.h"

namespace ui {
namespace {

constexpr const char *kNoUndo = "This cannot be undone without a backup.";

std::string withClass(const std::string &ns) { return ns + " (" + nvsm::className(nvsm::classify(ns)) + ")"; }

std::string warningText(const char *warning) {
    return warning != nullptr ? std::string(warning) + " " + kNoUndo : std::string(kNoUndo);
}

// Checked again when the change runs: Advanced mode may have locked while the
// confirmation was open.
bool stillAllowed(UiController &ui, app::Action action, const std::string &ns) {
    const app::Permission p = app::permissionFor(action, nvsm::classify(ns), ui.advanced());
    if (!p.allowed) showNotAllowed(ui, p.denied);
    return p.allowed;
}

void showOutcome(UiController &ui, const std::string &title, const app::ChangeOutcome &outcome) {
    ui.modelChanged();
    ui.push(std::unique_ptr<Screen>(new ResultScreen(ui, describeChange(title, outcome))));
}

} // namespace

void showNotAllowed(UiController &ui, const std::string &reason) {
    ResultInfo info;
    info.ok = false;
    info.title = "Not allowed";
    info.badge = "LOCKED";
    info.lines.push_back(reason);
    ui.push(std::unique_ptr<Screen>(new ResultScreen(ui, std::move(info))));
}

void confirmDeleteNamespace(UiController &ui, const nvsm::NamespaceInfo &ns) {
    const app::Permission p = app::permissionFor(app::Action::DeleteNamespace, nvsm::classify(ns.name), ui.advanced());
    if (!p.allowed) {
        showNotAllowed(ui, p.denied);
        return;
    }
    char counts[64];
    snprintf(counts, sizeof counts, "Keys: %u   Entries: %u", static_cast<unsigned>(ns.keyCount),
             static_cast<unsigned>(ns.usedEntries));

    ConfirmRequest req;
    req.title = "Delete namespace?";
    req.lines = {withClass(ns.name), counts, "All keys are erased; the namespace's own entry stays."};
    req.warning = warningText(p.warning);
    req.confirmation = p.confirmation;
    const std::string name = ns.name;
    req.onConfirm = [name](UiController &ui) {
        if (!stillAllowed(ui, app::Action::DeleteNamespace, name)) return;
        showOutcome(ui, "Namespace deleted", ui.model().eraseNamespace(name));
    };
    ui.push(std::unique_ptr<Screen>(new ConfirmScreen(ui, std::move(req))));
}

void confirmDeleteKey(UiController &ui, const std::string &ns, const nvsm::KeyInfo &key, int popsAfter) {
    const app::Permission p = app::permissionFor(app::Action::DeleteKey, nvsm::classify(ns), ui.advanced());
    if (!p.allowed) {
        showNotAllowed(ui, p.denied);
        return;
    }

    ConfirmRequest req;
    req.title = "Delete key?";
    req.lines = {withClass(ns), "Key: " + key.name + " (" + nvsm::typeName(key.type) + ")"};
    req.warning = warningText(p.warning);
    req.confirmation = p.confirmation;
    const std::string name = key.name;
    req.onConfirm = [ns, name, popsAfter](UiController &ui) {
        if (!stillAllowed(ui, app::Action::DeleteKey, ns)) return;
        const app::ChangeOutcome outcome = ui.model().eraseKey(ns, name);
        for (int i = 0; i < popsAfter; ++i) ui.pop();
        showOutcome(ui, "Key deleted", outcome);
    };
    ui.push(std::unique_ptr<Screen>(new ConfirmScreen(ui, std::move(req))));
}

void startEdit(UiController &ui, const std::string &ns, const nvsm::KeyInfo &key) {
    if (!app::editable(key.type)) {
        showNotAllowed(ui, "Blob values are read-only in this version.");
        return;
    }
    const app::Permission p = app::permissionFor(app::Action::EditKey, nvsm::classify(ns), ui.advanced());
    if (!p.allowed) {
        showNotAllowed(ui, p.denied);
        return;
    }
    ui.push(std::unique_ptr<Screen>(new EditValueScreen(ui, ns, key)));
}

void confirmEdit(UiController &ui, const std::string &ns, const nvsm::KeyInfo &key, const std::string &currentText,
                 const nvsm::Value &newValue) {
    const app::Permission p = app::permissionFor(app::Action::EditKey, nvsm::classify(ns), ui.advanced());
    if (!p.allowed) {
        showNotAllowed(ui, p.denied);
        return;
    }
    const std::string newText =
        newValue.type == nvsm::ValueType::Str ? "\"" + newValue.str + "\"" : formatDecimal(newValue);

    ConfirmRequest req;
    req.title = "Save " + key.name + "?";
    req.lines = {withClass(ns), "Now: " + currentText, "New: " + newText};
    req.warning = warningText(p.warning);
    req.confirmation = p.confirmation;
    const std::string name = key.name;
    req.onConfirm = [ns, name, newValue](UiController &ui) {
        if (!stillAllowed(ui, app::Action::EditKey, ns)) return;
        const app::ChangeOutcome outcome = ui.model().setValue(ns, name, newValue);
        ui.pop(); // the edit screen
        showOutcome(ui, "Saved", outcome);
    };
    ui.push(std::unique_ptr<Screen>(new ConfirmScreen(ui, std::move(req))));
}

void toggleAdvancedMode(UiController &ui) {
    if (ui.advanced()) {
        ui.setAdvanced(false);
        return;
    }
    ConfirmRequest req;
    req.title = "Advanced mode";
    req.lines = {"Unlocks changing and deleting SYSTEM namespaces such as Wi-Fi and PHY calibration.",
                 "Locks again after 5 minutes without a key press, on reboot, or with A."};
    req.warning = "A mistake here can break Wi-Fi or radio calibration for every firmware on this device.";
    req.confirmation = app::Confirmation::Hold;
    req.onConfirm = [](UiController &ui) { ui.setAdvanced(true); };
    ui.push(std::unique_ptr<Screen>(new ConfirmScreen(ui, std::move(req))));
}

void confirmExit(UiController &ui) {
    ConfirmRequest req;
    req.title = "Exit NVS Manager?";
    req.lines = {"The device restarts. Nothing is written.",
                 "Under Launcher this opens NVS Manager again. To get back to Launcher, power the device "
                 "off: switch off, USB cable out."};
    req.confirmation = app::Confirmation::Press;
    req.onConfirm = [](UiController &) { platform::restart(); };
    ui.push(std::unique_ptr<Screen>(new ConfirmScreen(ui, std::move(req))));
}

} // namespace ui

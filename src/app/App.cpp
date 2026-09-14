#include "app/App.h"

#include <memory>

#include "app/NvsModel.h"
#include "platform/Platform.h"
#include "ui/UiController.h"

namespace app {
namespace {

std::unique_ptr<NvsModel> g_model;
std::unique_ptr<ui::UiController> g_ui;

} // namespace

void setup() {
    platform::begin();
    platform::logf("BOOT", "NVS Manager %s on %s", NVSM_VERSION, platform::deviceName());
    platform::logBootDiagnostics();

    g_model.reset(new NvsModel("nvs"));
    g_model->refresh();

    // Show the screen before the long NVS report: logging is best-effort and
    // must never be what the user waits for.
    g_ui.reset(new ui::UiController(*g_model));
    g_ui->begin();
    g_ui->render();
    platform::logf("BOOT", "UI ready %u ms after start", static_cast<unsigned>(platform::millis()));

    g_model->logReport();
}

void loop() {
    platform::KeyEvent ev;
    while (platform::pollKey(ev)) g_ui->handleKey(ev);
    g_ui->update();
    g_ui->render();
    platform::delay(10);
}

} // namespace app

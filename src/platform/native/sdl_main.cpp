// Desktop entry point: M5GFX runs the SDL event loop on the main thread and
// our Arduino-style setup()/loop() on a worker thread.

#include <M5GFX.h>

#if defined(SDL_h_)

void setup();
void loop();

namespace {

int runApp(bool *running) {
    setup();
    do {
        loop();
    } while (*running);
    return 0;
}

} // namespace

int main(int, char **) {
    // Panel_sdl rotates the window on R/L and zooms on 1-6 when the modifier
    // state equals this value. The default (no modifier) would steal keys the
    // application needs, so require Left Ctrl for those shortcuts.
    lgfx::Panel_sdl::setShortcutKeymod(KMOD_LCTRL);
    return lgfx::Panel_sdl::main(runApp, 128);
}

#endif

// Arduino-style entry points. On the device the Arduino core calls them; in
// the native build platform/native/sdl_main.cpp does.

#include "app/App.h"

void setup() { app::setup(); }

void loop() { app::loop(); }

#pragma once

enum RgbMode {
  RGB_MODE_OFF = 0,
  RGB_MODE_STATIC,
  RGB_MODE_RAINBOW,
  RGB_MODE_RAINBOW_CYCLE,
  RGB_MODE_BREATHE,
  RGB_MODE_CHASE,
  RGB_MODE_COUNT
};

// Attaches the LED strip on RGB_LED_PIN. Call once in setup(), before prefs
// are loaded, so the strip is in a known (driven, blanked) state as early as
// relayForceOff() puts the relay pin in one on a relay module.
void rgbInit();

// Reads power/mode/brightness/speed/color from prefs. Call after rgbInit().
void rgbLoadConfig();

void rgbSet(bool on);

// Registers /on, /off, /mode, /color, /brightness, /speed. Call only when
// this module's moduleType is "rgb".
void rgbRegisterRoutes();

// Advances whichever animation is currently selected and pushes it to the
// strip; rate-limited internally by rgbSpeed. Call once per loop() iteration
// unconditionally - it no-ops immediately when the strip is off.
void rgbLoop();

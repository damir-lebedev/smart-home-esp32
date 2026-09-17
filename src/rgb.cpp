#include "rgb.h"
#include "globals.h"
#include "ws_events.h"

#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

static const uint8_t RGB_LED_PIN_DEFAULT = 0;
static const int RGB_NUM_LEDS = 570;

// Hard cap at 85% of full brightness (255) to keep current draw down — strips
// pushed to 100% cook their own diodes, and a dead WS2812 takes out every LED
// after it on the strip until it's desoldered and replaced.
static const uint8_t RGB_MAX_BRIGHTNESS = 216;

static uint8_t clampBrightness(uint16_t b) {
  return (uint8_t)(b > RGB_MAX_BRIGHTNESS ? RGB_MAX_BRIGHTNESS : b);
}

static CRGB leds[RGB_NUM_LEDS];

// Animation-internal state; doesn't need to be visible outside this file.
static uint32_t lastStep = 0;
static uint16_t step = 0;
static uint8_t hue = 0;

static CRGB colorFromHex(uint32_t c) {
  return CRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// FastLED needs the data pin as a compile-time template argument, so a
// runtime-configured pin (see "ledpin" in prefs, set via /config) has to be
// dispatched through a switch rather than passed as a plain variable. Boards
// wired up differently (a hardware revision using another GPIO for the strip)
// pick their pin here instead of needing a firmware fork.
static void addLedsOnPin(uint8_t pin) {
  switch (pin) {
    case 0: FastLED.addLeds<WS2812, 0, GRB>(leds, RGB_NUM_LEDS); break;
    case 1: FastLED.addLeds<WS2812, 1, GRB>(leds, RGB_NUM_LEDS); break;
    case 2: FastLED.addLeds<WS2812, 2, GRB>(leds, RGB_NUM_LEDS); break;
    case 3: FastLED.addLeds<WS2812, 3, GRB>(leds, RGB_NUM_LEDS); break;
    case 4: FastLED.addLeds<WS2812, 4, GRB>(leds, RGB_NUM_LEDS); break;
    case 5: FastLED.addLeds<WS2812, 5, GRB>(leds, RGB_NUM_LEDS); break;
    case 6: FastLED.addLeds<WS2812, 6, GRB>(leds, RGB_NUM_LEDS); break;
    case 7: FastLED.addLeds<WS2812, 7, GRB>(leds, RGB_NUM_LEDS); break;
    case 8: FastLED.addLeds<WS2812, 8, GRB>(leds, RGB_NUM_LEDS); break;
    case 9: FastLED.addLeds<WS2812, 9, GRB>(leds, RGB_NUM_LEDS); break;
    case 10: FastLED.addLeds<WS2812, 10, GRB>(leds, RGB_NUM_LEDS); break;
    default: FastLED.addLeds<WS2812, RGB_LED_PIN_DEFAULT, GRB>(leds, RGB_NUM_LEDS); break;
  }
}

void rgbInit() {
  addLedsOnPin(prefs.getUChar("ledpin", RGB_LED_PIN_DEFAULT));
  fill_solid(leds, RGB_NUM_LEDS, CRGB::Black);
  FastLED.show();
}

void rgbLoadConfig() {
  rgbPower = prefs.getBool("power", true);
  rgbBrightness = clampBrightness(prefs.getUChar("bright", 120));
  rgbMode = prefs.getUChar("mode", RGB_MODE_STATIC);
  rgbSpeed = prefs.getUChar("speed", 128);
  rgbColorHex = prefs.getUInt("color", 0xFFFFFF);
}

void rgbSet(bool on) {
  rgbPower = on;
  prefs.putBool("power", rgbPower);
  broadcastPowerState(WiFi.localIP().toString(), rgbPower);
}

void rgbRegisterRoutes() {
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest* r) {
    rgbSet(true);
    r->send(200, "text/plain; charset=utf-8", "ON");
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest* r) {
    rgbSet(false);
    r->send(200, "text/plain; charset=utf-8", "OFF");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest* r) {
    rgbSet(!rgbPower);
    r->send(200, "text/plain; charset=utf-8", rgbPower ? "ON" : "OFF");
  });

  server.on("/mode", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (r->hasParam("set")) {
      uint8_t m = r->getParam("set")->value().toInt();
      rgbMode = (m < RGB_MODE_COUNT) ? m : RGB_MODE_STATIC;
      prefs.putUChar("mode", rgbMode);
    }
    r->send(200, "text/plain; charset=utf-8", "OK");
  });

  server.on("/color", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (r->hasParam("hex")) {
      rgbColorHex = strtoul(r->getParam("hex")->value().c_str(), NULL, 16);
      prefs.putUInt("color", rgbColorHex);
    }
    r->send(200, "text/plain; charset=utf-8", "OK");
  });

  server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (r->hasParam("val")) {
      rgbBrightness = clampBrightness(r->getParam("val")->value().toInt());
      prefs.putUChar("bright", rgbBrightness);
    }
    r->send(200, "text/plain; charset=utf-8", "OK");
  });

  server.on("/speed", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (r->hasParam("val")) {
      rgbSpeed = r->getParam("val")->value().toInt();
      prefs.putUChar("speed", rgbSpeed);
    }
    r->send(200, "text/plain; charset=utf-8", "OK");
  });
}

void rgbLoop() {
  if (!rgbPower || rgbMode == RGB_MODE_OFF) {
    fill_solid(leds, RGB_NUM_LEDS, CRGB::Black);
    FastLED.show();
    return;
  }

  uint16_t effectMs = map(255 - rgbSpeed, 0, 255, 10, 200);
  if (millis() - lastStep < effectMs) return;
  lastStep = millis();
  step++;

  FastLED.setBrightness(rgbBrightness);

  switch (rgbMode) {
    case RGB_MODE_STATIC:
      fill_solid(leds, RGB_NUM_LEDS, colorFromHex(rgbColorHex));
      break;

    case RGB_MODE_RAINBOW:
      fill_rainbow(leds, RGB_NUM_LEDS, hue++);
      break;

    case RGB_MODE_RAINBOW_CYCLE:
      for (int i = 0; i < RGB_NUM_LEDS; i++) {
        leds[i] = CHSV((i * 255 / RGB_NUM_LEDS + hue) & 255, 255, 255);
      }
      hue++;
      break;

    case RGB_MODE_BREATHE: {
      uint8_t b = sin8(step * 2);
      FastLED.setBrightness((uint16_t)b * rgbBrightness / 255);
      fill_solid(leds, RGB_NUM_LEDS, colorFromHex(rgbColorHex));
      break;
    }

    case RGB_MODE_CHASE: {
      int pos = step % RGB_NUM_LEDS;
      fill_solid(leds, RGB_NUM_LEDS, CRGB::Black);
      leds[pos] = colorFromHex(rgbColorHex);
      leds[(pos + 1) % RGB_NUM_LEDS] = colorFromHex(rgbColorHex).nscale8(120);
      leds[(pos + RGB_NUM_LEDS - 1) % RGB_NUM_LEDS] = colorFromHex(rgbColorHex).nscale8(120);
      break;
    }
  }

  FastLED.show();
}

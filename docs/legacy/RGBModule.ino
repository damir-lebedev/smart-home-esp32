#define FASTLED_ALLOW_INTERRUPTS 0

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <FastLED.h>

// ─────────────────────────────────────
// LED CONFIG
// ─────────────────────────────────────
#define LED_PIN     0
#define NUM_LEDS    570
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

// ─────────────────────────────────────
// STATE
// ─────────────────────────────────────
bool power = true;
uint8_t brightness = 120;
uint8_t mode = 1;
uint8_t speed = 128;
uint32_t colorHex = 0xFFFFFF;

enum {
  MODE_OFF = 0,
  MODE_STATIC,
  MODE_RAINBOW,
  MODE_RAINBOW_CYCLE,
  MODE_BREATHE,
  MODE_CHASE,
  MODE_COUNT
};

// animation helpers
uint32_t lastStep = 0;
uint16_t step = 0;
uint8_t hue = 0;

// ─────────────────────────────────────
AsyncWebServer server(80);
Preferences prefs;

String deviceName = "RGB Подсветка";
String moduleType = "rgb";

// ─────────────────────────────────────
// UTILS
// ─────────────────────────────────────
CRGB colorFromHex(uint32_t c) {
  return CRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// ─────────────────────────────────────
// LED ENGINE (non-blocking)
// ─────────────────────────────────────
void applyLed() {
  if (!power || mode == MODE_OFF) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    return;
  }

  uint16_t effectMs = map(255 - speed, 0, 255, 10, 200);
  if (millis() - lastStep < effectMs) return;
  lastStep = millis();
  step++;

  FastLED.setBrightness(brightness);

  switch (mode) {

    case MODE_STATIC:
      fill_solid(leds, NUM_LEDS, colorFromHex(colorHex));
      break;

    case MODE_RAINBOW:
      fill_rainbow(leds, NUM_LEDS, hue++);
      break;

    case MODE_RAINBOW_CYCLE:
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV((i * 255 / NUM_LEDS + hue) & 255, 255, 255);
      }
      hue++;
      break;

    case MODE_BREATHE: {
      uint8_t b = sin8(step * 2);
      FastLED.setBrightness((uint16_t)b * brightness / 255);
      fill_solid(leds, NUM_LEDS, colorFromHex(colorHex));
      break;
    }

    case MODE_CHASE: {
      int pos = step % NUM_LEDS;
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      leds[pos] = colorFromHex(colorHex);
      leds[(pos + 1) % NUM_LEDS] = colorFromHex(colorHex).nscale8(120);
      leds[(pos + NUM_LEDS - 1) % NUM_LEDS] = colorFromHex(colorHex).nscale8(120);
      break;
    }
  }

  FastLED.show();
}

// ─────────────────────────────────────
// UI FRAGMENT (for master)
// ─────────────────────────────────────
String getUiFragment() {
  return R"rawliteral(
<h3>🌈 {{name}}</h3>
<div class="status">RGB подсветка</div>
<div class="indicator">💡</div>

<div class="buttons">
  <button class="btn on" onclick="cmd('{{ip}}','on')">ВКЛ</button>
  <button class="btn off" onclick="cmd('{{ip}}','off')">ВЫКЛ</button>
</div>

<div style="margin-top:10px">
  <select onchange="fetch('http://{{ip}}/mode?set='+this.value)">
    <option value="1">🎨 Статичный</option>
    <option value="2">🌈 Радуга</option>
    <option value="3">🌈 Цикл</option>
    <option value="4">🌬️ Дыхание</option>
    <option value="5">🏃 Chase</option>
  </select>
</div>

<div style="margin-top:10px">
  <input type="color"
    onchange="fetch('http://{{ip}}/color?hex='+this.value.replace('#',''))">
</div>

<div style="margin-top:10px">
  <input type="range" min="5" max="255"
    oninput="fetch('http://{{ip}}/brightness?val='+this.value)">
</div>

<div style="margin-top:10px">
  <input type="range" min="0" max="255"
    oninput="fetch('http://{{ip}}/speed?val='+this.value)">
</div>
)rawliteral";
}

// ─────────────────────────────────────
// SETUP PAGE (AP MODE)
// ─────────────────────────────────────
const char* setupPage = R"rawliteral(
<!DOCTYPE html><html lang="ru">
<head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width">
<title>Настройка RGB</title>
<style>
body{font-family:sans-serif;max-width:400px;margin:40px auto}
input,button{width:100%;padding:12px;margin:8px 0}
button{background:#1a73e8;color:#fff;border:none}
</style></head>
<body>
<h2>Настройка RGB модуля</h2>
<form method="POST" action="/save">
<input name="ssid" placeholder="WiFi SSID" required>
<input name="pass" placeholder="Пароль">
<input name="name" placeholder="Имя модуля">
<button>Сохранить</button>
</form>
</body></html>
)rawliteral";

// ─────────────────────────────────────
// SETUP
// ─────────────────────────────────────
void setup() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);

  prefs.begin("rgb", false);
  power      = prefs.getBool("power", true);
  brightness = prefs.getUChar("bright", 120);
  mode       = prefs.getUChar("mode", MODE_STATIC);
  speed      = prefs.getUChar("speed", 128);
  colorHex   = prefs.getUInt("color", 0xFFFFFF);
  deviceName = prefs.getString("name", "RGB Подсветка");

  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  // ─── AP MODE ───
  if (ssid == "") {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("RGB_Setup");

    server.on("/", HTTP_GET, [](auto*r){
      r->send(200, "text/html", setupPage);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest*r){
      prefs.putString("ssid", r->getParam("ssid", true)->value());
      prefs.putString("pass", r->getParam("pass", true)->value());
      prefs.putString("name", r->getParam("name", true)->value());
      r->send(200, "text/plain", "Saved. Reboot...");
      delay(800);
      ESP.restart();
    });

    server.begin();
    return;
  }

  // ─── STA MODE ───
  WiFi.begin(ssid.c_str(), pass.c_str());
  while (WiFi.status() != WL_CONNECTED) delay(300);

  // ─── API ───
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r){
    DynamicJsonDocument doc(512);
    doc["api"]  = 1;
    doc["type"] = moduleType;
    doc["name"] = deviceName;

    JsonObject st = doc.createNestedObject("state");
    st["power"] = power;
    st["mode"] = mode;
    st["brightness"] = brightness;
    st["color"] = colorHex;
    st["speed"] = speed;

    String out;
    serializeJson(doc, out);
    auto resp = r->beginResponse(200, "application/json", out);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    r->send(resp);
  });

  server.on("/ui", HTTP_GET, [](AsyncWebServerRequest *r){
    String f = getUiFragment();
    String ip = WiFi.localIP().toString();
    f.replace("{{name}}", deviceName);
    f.replace("{{ip}}", ip);
    auto resp = r->beginResponse(200, "text/html", f);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    r->send(resp);
  });

  server.on("/on", HTTP_GET, [](auto*r){
    power = true;
    prefs.putBool("power", true);
    r->send(200);
  });

  server.on("/off", HTTP_GET, [](auto*r){
    power = false;
    prefs.putBool("power", false);
    r->send(200);
  });

  server.on("/mode", HTTP_GET, [](AsyncWebServerRequest*r){
    if (r->hasParam("set")) {
      mode = r->getParam("set")->value().toInt();
      if (mode >= MODE_COUNT) mode = MODE_STATIC;
      prefs.putUChar("mode", mode);
    }
    r->send(200);
  });

  server.on("/speed", HTTP_GET, [](AsyncWebServerRequest*r){
    if (r->hasParam("val")) {
      speed = r->getParam("val")->value().toInt();
      prefs.putUChar("speed", speed);
    }
    r->send(200);
  });

  server.on("/color", HTTP_GET, [](AsyncWebServerRequest*r){
    if (r->hasParam("hex")) {
      colorHex = strtoul(r->getParam("hex")->value().c_str(), NULL, 16);
      prefs.putUInt("color", colorHex);
    }
    r->send(200);
  });

  server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest*r){
    if (r->hasParam("val")) {
      brightness = r->getParam("val")->value().toInt();
      prefs.putUChar("bright", brightness);
    }
    r->send(200);
  });

  server.begin();
}

// ─────────────────────────────────────
void loop() {
  applyLed();
}
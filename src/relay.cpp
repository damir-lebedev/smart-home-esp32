#include "relay.h"
#include "globals.h"
#include "ws_events.h"

// Most relay boards are active-LOW (LOW = energized/ON). Some hardware
// revisions wire it the other way around; "invert" in prefs lets a single
// shared firmware image drive either without special-casing it network-wide.
static bool relayInverted = false;

static int activeLevel() {
  return relayInverted ? HIGH : LOW;
}

static int idleLevel() {
  return relayInverted ? LOW : HIGH;
}

void relayLoadConfig() {
  relayInverted = prefs.getBool("invert", false);
}

void relayForceOff() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, idleLevel());
}

void relayApplyState() {
  digitalWrite(RELAY_PIN, relayState ? activeLevel() : idleLevel());
}

void relaySet(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, relayState ? activeLevel() : idleLevel());
  prefs.putBool("relay", relayState);
  broadcastRelayState(WiFi.localIP().toString(), relayState);
}

void relayRegisterRoutes() {
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest* r) {
    relaySet(true);
    r->send(200, "text/plain; charset=utf-8", "ON");
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest* r) {
    relaySet(false);
    r->send(200, "text/plain; charset=utf-8", "OFF");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest* r) {
    relaySet(!relayState);
    r->send(200, "text/plain; charset=utf-8", relayState ? "ON" : "OFF");
  });
}

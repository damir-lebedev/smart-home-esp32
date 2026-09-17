#include "relay.h"
#include "globals.h"
#include "ws_events.h"

void relayForceOff() {
  pinMode(RELAY_PIN, OUTPUT);
  // Relay board is active-LOW: LOW = energized/ON, HIGH = OFF.
  digitalWrite(RELAY_PIN, HIGH);
}

void relayApplyState() {
  digitalWrite(RELAY_PIN, relayState ? LOW : HIGH);
}

void relaySet(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, relayState ? LOW : HIGH);
  prefs.putBool("relay", relayState);
  broadcastRelayState(WiFi.localIP().toString(), relayState);
}

void relayRegisterRoutes() {
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest* r) {
    relaySet(true);
    r->send(200, "text/plain", "ON");
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest* r) {
    relaySet(false);
    r->send(200, "text/plain", "OFF");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest* r) {
    relaySet(!relayState);
    r->send(200, "text/plain", relayState ? "ON" : "OFF");
  });
}

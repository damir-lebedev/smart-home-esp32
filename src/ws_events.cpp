#include "ws_events.h"
#include "globals.h"
#include <ArduinoJson.h>

void wsOnEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    broadcastAllStates();
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
}

void broadcastPowerState(const String& moduleIp, bool power) {
  if (ws.count() == 0) return;

  JsonDocument doc;
  doc["event"] = "state";
  doc["ip"] = moduleIp;
  doc["power"] = power;

  String msg;
  serializeJson(doc, msg);
  ws.textAll(msg);
}

void broadcastAllStates() {
  broadcastPowerState(WiFi.localIP().toString(), moduleType == "rgb" ? rgbPower : relayState);
  // Other modules broadcast their own state on change; the master doesn't
  // poll them just to fill a newly-connected client in.
}

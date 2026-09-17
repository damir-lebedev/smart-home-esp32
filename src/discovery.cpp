#include "discovery.h"
#include "globals.h"
#include <ArduinoJson.h>

static unsigned long lastAnnounce = 0;

static void announceSelf() {
  JsonDocument doc;
  doc["cmd"] = "announce";
  doc["name"] = deviceName;
  doc["type"] = moduleType;
  doc["ip"] = WiFi.localIP().toString();

  String packet;
  serializeJson(doc, packet);

  udp.beginPacket(broadcastIP, UDP_PORT);
  udp.print(packet);
  udp.endPacket();
}

static void pollAnnouncements() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buf[256] = {0};
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len > 0) {
      String msg = String(buf);
      msg.trim();

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, msg);
      if (!err) {
        String cmd = doc["cmd"] | "";
        if (cmd == "announce") {
          String name = doc["name"] | "Unknown";
          String type = doc["type"] | "unknown";
          String ip = doc["ip"] | "";

          if (ip.length() > 6 && ip != WiFi.localIP().toString()) {
            ModuleInfo info;
            info.name = name;
            info.type = type;
            info.lastSeen = millis();
            discoveredModules[ip] = info;
          }
        }
      }
    }
  }

  unsigned long now = millis();
  for (auto it = discoveredModules.begin(); it != discoveredModules.end();) {
    if (now - it->second.lastSeen > MODULE_TIMEOUT) {
      Serial.println("Timeout removed: " + it->first);
      it = discoveredModules.erase(it);
    } else {
      ++it;
    }
  }
}

void discoveryLoop() {
  if (role == "slave") {
    if (millis() - lastAnnounce >= ANNOUNCE_INTERVAL) {
      lastAnnounce = millis();
      announceSelf();
    }
  } else if (role == "master") {
    pollAnnouncements();
  }
}

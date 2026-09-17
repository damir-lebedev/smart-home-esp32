#include "discovery.h"
#include "globals.h"
#include <ArduinoJson.h>

static unsigned long lastAnnounce = 0;

static String knownMasterIp;
static String knownMasterName;
static unsigned long knownMasterLastSeen = 0;

bool discoveryGetMaster(String& ip, String& name) {
  if (knownMasterIp.length() == 0) return false;
  if (millis() - knownMasterLastSeen > MODULE_TIMEOUT) return false;
  ip = knownMasterIp;
  name = knownMasterName;
  return true;
}

// Every module announces itself so the others can find it: a master needs
// this to build its module list, and a slave needs it so it can show a link
// back to whichever module is currently acting as master.
static void announcePresence() {
  JsonDocument doc;
  doc["cmd"] = (role == "master") ? "master_here" : "announce";
  doc["name"] = deviceName;
  doc["type"] = moduleType;
  doc["ip"] = WiFi.localIP().toString();

  String packet;
  serializeJson(doc, packet);

  udp.beginPacket(broadcastIP, UDP_PORT);
  udp.print(packet);
  udp.endPacket();
}

static void pollIncoming() {
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
        String ip = doc["ip"] | "";
        String name = doc["name"] | "Unknown";

        if (ip.length() > 6 && ip != WiFi.localIP().toString()) {
          if (cmd == "announce" && role == "master") {
            String type = doc["type"] | "unknown";
            ModuleInfo info;
            info.name = name;
            info.type = type;
            info.lastSeen = millis();
            discoveredModules[ip] = info;
          } else if (cmd == "master_here") {
            knownMasterIp = ip;
            knownMasterName = name;
            knownMasterLastSeen = millis();
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
  if (millis() - lastAnnounce >= ANNOUNCE_INTERVAL) {
    lastAnnounce = millis();
    announcePresence();
  }
  pollIncoming();
}

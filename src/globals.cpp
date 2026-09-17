#include "globals.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;
WiFiUDP udp;

IPAddress broadcastIP;

const int RELAY_PIN = 1;
bool relayState = false;

String deviceName = "Module";
String role = "slave";
String moduleType = "relay";

std::map<String, ModuleInfo> discoveredModules;

const int UDP_PORT = 12345;
const unsigned long ANNOUNCE_INTERVAL = 5000;
const unsigned long MODULE_TIMEOUT = 15000;

const char* apSSID = "SmartModule_Setup";
const IPAddress apIP(192, 168, 4, 1);
const IPAddress apSubnet(255, 255, 255, 0);

String otaKey() {
  return prefs.getString("otakey", "");
}

bool adminKeyOk(AsyncWebServerRequest* r) {
  String required = otaKey();
  if (required.length() == 0) return true;
  String provided;
  if (r->hasHeader("X-OTA-Key")) provided = r->getHeader("X-OTA-Key")->value();
  else if (r->hasParam("key")) provided = r->getParam("key")->value();
  return provided == required;
}

String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

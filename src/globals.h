#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <map>

// Bump this string before every build you intend to flash. It is reported by
// /status on every module, so you can always tell which firmware a device is
// actually running (and the network-wide OTA rollout uses it to confirm a
// device has actually rebooted into the new build).
#define FIRMWARE_VERSION "2026.09.17-10"

extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern Preferences prefs;
extern WiFiUDP udp;

extern IPAddress broadcastIP;

extern const int RELAY_PIN;
extern bool relayState;

extern String deviceName;
extern String role;          // "slave" or "master"
extern String moduleType;    // "relay" for now

struct ModuleInfo {
  String name;
  String type;
  unsigned long lastSeen;
};

// Master-only: IP -> discovered slave info.
extern std::map<String, ModuleInfo> discoveredModules;

extern const int UDP_PORT;
extern const unsigned long ANNOUNCE_INTERVAL;
extern const unsigned long MODULE_TIMEOUT;

extern const char* apSSID;
extern const IPAddress apIP;
extern const IPAddress apSubnet;

// Shared secret required for admin actions on this device: triggering OTA
// and changing its settings via /config. Empty = open to anyone on the LAN
// (fine for a trusted home network, but set it once you have more than a
// couple of modules). The same key must be set on every module, since the
// master uses it when it talks to the others.
String otaKey();

// Checks a request's "key" query param / "X-OTA-Key" header against otaKey().
// Always true if no key is configured.
bool adminKeyOk(AsyncWebServerRequest* r);

// Percent-encodes a URL for safe embedding as a query-string value.
String urlEncode(const String& s);

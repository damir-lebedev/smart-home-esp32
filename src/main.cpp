#include "globals.h"
#include "relay.h"
#include "net_setup.h"
#include "discovery.h"
#include "ws_events.h"
#include "web_ui.h"
#include "ota.h"
#include "health.h"

void setup() {
  healthInitWatchdog();
  relayForceOff();

  prefs.begin("smartmod", false);

  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  deviceName = prefs.getString("name", "Module");
  role = prefs.getString("role", "slave");
  relayState = prefs.getBool("relay", false);
  relayApplyState();

  if (savedSsid == "") {
    netStartApMode();
    return;
  }

  if (!netConnectSTA(savedSsid, savedPass)) {
    prefs.clear();
    ESP.restart();
  }

  Serial.begin(115200);
  Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
  Serial.println("Subnet: " + WiFi.subnetMask().toString());
  Serial.println("Firmware: " FIRMWARE_VERSION);

  netComputeBroadcastIP();
  Serial.print("Calculated broadcast IP: ");
  Serial.println(broadcastIP);

  udp.begin(UDP_PORT);

  webUiRegisterCommonRoutes();
  relayRegisterRoutes();
  netRegisterConfigRoutes();

  if (role == "master") {
    webUiRegisterMasterRoutes();
  } else {
    webUiRegisterSlaveRoute();
  }

  otaBegin();

  ws.onEvent(wsOnEvent);
  server.addHandler(&ws);

  server.begin();
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
}

void loop() {
  healthFeedWatchdog();
  healthWatchWifi();
  netTick();
  discoveryLoop();
  otaLoop();
}

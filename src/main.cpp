#include "globals.h"
#include "relay.h"
#include "rgb.h"
#include "net_setup.h"
#include "discovery.h"
#include "ws_events.h"
#include "web_ui.h"
#include "ota.h"
#include "health.h"

void setup() {
  healthInitWatchdog();

  prefs.begin("smartmod", false);
  moduleType = prefs.getString("type", "relay");

  // Load config and put the output hardware in a known, safe state (blanked
  // strip / de-energized relay) before anything else touches it — in
  // particular before WiFi comes up and remote commands become possible.
  if (moduleType == "rgb") {
    rgbInit();
    rgbLoadConfig();
  } else {
    relayLoadConfig();  // must run first: relayForceOff() needs "invert" to know which level is actually off
    relayForceOff();
  }

  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  deviceName = prefs.getString("name", "Module");
  role = prefs.getString("role", "slave");

  if (moduleType != "rgb") {
    relayState = prefs.getBool("relay", false);
    relayApplyState();
  }

  // No SSID saved yet (factory state, or after the prefs.clear() below) —
  // become the setup AP and stop here; setup() resumes fresh after the
  // config form reboots the device with credentials in place.
  if (savedSsid == "") {
    netStartApMode();
    return;
  }

  // One failed connection attempt wipes every setting on this device (name,
  // role, RGB pin, OTA key, ...), not just the WiFi credentials — a genuinely
  // bad password needs this to escape a boot loop, but it means a device in
  // a weak-signal spot that times out on a bad day comes back as a blank
  // module in AP mode, needing a full reconfigure via /config.
  if (!netConnectSTA(savedSsid, savedPass)) {
    prefs.clear();
    ESP.restart();
  }

  // Native USB CDC, not a UART bridge: nothing printed here reaches a
  // terminal until this line runs, so a device that never gets this far
  // (still negotiating WiFi, or wedged before it) looks completely silent
  // over serial even while it's still alive and retrying.
  Serial.begin(115200);
  Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
  Serial.println("Subnet: " + WiFi.subnetMask().toString());
  Serial.println("Firmware: " FIRMWARE_VERSION);

  netComputeBroadcastIP();
  Serial.print("Calculated broadcast IP: ");
  Serial.println(broadcastIP);

  udp.begin(UDP_PORT);

  webUiRegisterCommonRoutes();
  if (moduleType == "rgb") {
    rgbRegisterRoutes();
  } else {
    relayRegisterRoutes();
  }
  netRegisterConfigRoutes();

  webUiRegisterDashboardRoute();

  otaBegin();

  ws.onEvent(wsOnEvent);
  server.addHandler(&ws);

  server.begin();
  // The dashboard on one module's IP calls the API on every other module's
  // IP directly from the browser (see cmd()/updateStatus() in web_ui.cpp) —
  // without this, those are cross-origin requests the browser would refuse
  // to let the page read the response of.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
}

void loop() {
  healthFeedWatchdog();
  healthWatchWifi();
  netTick();
  discoveryLoop();
  otaLoop();
  if (moduleType == "rgb") rgbLoop();  // relay has nothing to animate; it only reacts to /on, /off, /toggle
}

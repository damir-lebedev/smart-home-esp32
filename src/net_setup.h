#pragma once

#include <Arduino.h>

// Starts AP mode with the captive setup page (/, POST /save) and returns.
// Used when no WiFi credentials are stored yet.
void netStartApMode();

// Connects to the stored WiFi network. Returns false (and the caller should
// wipe prefs + restart) if the connection didn't come up within the timeout.
bool netConnectSTA(const String& ssid, const String& pass);

// Computes broadcastIP for the current STA connection. Call after netConnectSTA.
void netComputeBroadcastIP();

// Registers GET/POST /config: lets an already-configured module (any role)
// be renamed, re-roled, or re-keyed remotely without USB access. Gated by
// adminKeyOk() when an OTA key is set. Call once STA is up.
void netRegisterConfigRoutes();

// Applies a restart scheduled by the /save or /config handler. Call once per
// loop() iteration. Restarting from loop() instead of from inside the async
// request handler gives AsyncTCP time to actually flush the HTTP response
// before the reboot tears the connection down.
void netTick();

#pragma once

// Call once, first thing in setup(), before anything else. Subscribes the
// main loop task to the hardware watchdog: if loop() ever stops coming back
// (a wedged AsyncWebServer/heap issue, a stuck library call, ...) the chip
// reboots on its own instead of needing someone to pull the plug.
void healthInitWatchdog();

// Call once per loop() iteration, unconditionally. Also call anywhere in
// setup() that can legitimately block for a while (WiFi connect wait, ...) -
// the watchdog isn't fed automatically until loop() starts, so a slow
// setup() would otherwise trip it.
void healthFeedWatchdog();

// Self-OTA legitimately blocks for tens of seconds downloading + flashing.
// Unsubscribe from the watchdog around that call, then resubscribe once it
// returns (only reached if the update failed - success reboots on its own).
void healthSuspendWatchdogForLongOp();
void healthResumeWatchdogAfterLongOp();

// Call once per loop() iteration on STA-connected modules. If WiFi drops it
// first asks the radio to reconnect, and if that doesn't recover it within a
// couple of minutes (some ESP32 WiFi drivers do wedge, especially in noisy
// RF environments like a bathroom), it restarts the device outright.
void healthWatchWifi();

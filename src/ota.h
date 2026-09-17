#pragma once

// Mounts LittleFS (master only) and registers all OTA routes:
//  - /update              ElegantOTA portal, on every module (flash one device by hand)
//  - /fleet/pull          on every module, master uses it to trigger a self-update
//  - /fleet/upload        master only, receives the .bin to distribute
//  - /fleet/deploy        master only, starts a sequential rollout to all known modules
//  - /fleet/deploy/status master only, progress for the dashboard
// The custom routes live under /fleet/, not /ota/, because ElegantOTA itself
// reserves the whole /ota/* namespace (/ota/upload, /ota/start, /ota/metadata)
// - reusing it silently hijacks requests into ElegantOTA's own handler instead
// of this file's.
void otaBegin();

// Applies a pending self-update (any module) and advances the master's
// rollout state machine. Call once per loop() iteration.
void otaLoop();

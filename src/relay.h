#pragma once

// Reads the "invert" preference (set via /config) so relayForceOff() and
// relayApplyState() drive the correct physical level. Call after prefs is
// loaded but before relayForceOff().
void relayLoadConfig();

// Configures the pin and forces the relay off. Must be called after
// relayLoadConfig() (not before) — it needs "invert" already loaded to know
// which physical level is actually off, otherwise an inverted board would
// energize the relay instead of de-energizing it.
void relayForceOff();

// Drives the pin to match the currently loaded relayState. Call once
// relayState has been read from prefs.
void relayApplyState();

void relaySet(bool on);

// Registers /on, /off, /toggle. Available on every module regardless of role.
void relayRegisterRoutes();

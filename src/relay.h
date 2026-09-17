#pragma once

// Configures the pin and forces the relay off. Call first, before prefs is
// loaded, so the relay can't end up energized by an undefined pin state.
void relayForceOff();

// Drives the pin to match the currently loaded relayState. Call once
// relayState has been read from prefs.
void relayApplyState();

void relaySet(bool on);

// Registers /on, /off, /toggle. Available on every module regardless of role.
void relayRegisterRoutes();

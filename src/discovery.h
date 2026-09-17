#pragma once

#include <Arduino.h>

// Every module announces its own presence (as "announce" for a slave,
// "master_here" for a master) and listens for the others: a master collects
// slave announcements into discoveredModules, and any module remembers the
// last master it heard from. Call once per loop() iteration regardless of role.
void discoveryLoop();

// Fills ip/name and returns true if a master has announced itself recently
// (within MODULE_TIMEOUT). Returns false if none is currently known/reachable.
bool discoveryGetMaster(String& ip, String& name);

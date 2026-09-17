#pragma once

#include <Arduino.h>

// Every module announces its own presence (name/type/role/ip) and listens for
// the others: every module (not just the master) collects announcements into
// discoveredModules so any device's dashboard can show and control the whole
// fleet, and every module remembers the last master it heard from. Call once
// per loop() iteration regardless of role.
void discoveryLoop();

// Fills ip/name and returns true if a master has announced itself recently
// (within MODULE_TIMEOUT). Returns false if none is currently known/reachable.
bool discoveryGetMaster(String& ip, String& name);

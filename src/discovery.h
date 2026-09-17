#pragma once

// Slave: periodically broadcasts an announce packet.
// Master: reads announce packets into discoveredModules and expires stale ones.
// Call once per loop() iteration regardless of role.
void discoveryLoop();

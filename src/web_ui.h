#pragma once

// Registers routes available on every module: /status, /ui, /master, /modules.
void webUiRegisterCommonRoutes();

// Registers the full fleet dashboard at "/", identical on every module
// regardless of role — any module can control the whole house from its own
// page. Only the master's copy additionally exposes the network firmware
// panel, since /fleet/* stays gated to role=="master" in ota.cpp.
void webUiRegisterDashboardRoute();

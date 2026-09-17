#pragma once

// Registers routes available on every module: /status, /ui.
void webUiRegisterCommonRoutes();

// Registers the master-only dashboard: /, /modules.
void webUiRegisterMasterRoutes();

// Registers the plain-text landing page shown on a slave's own "/".
void webUiRegisterSlaveRoute();

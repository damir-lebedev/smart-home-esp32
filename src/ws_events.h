#pragma once

#include <ESPAsyncWebServer.h>

void wsOnEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);

// Sends this module's relay state to every connected dashboard client.
void broadcastRelayState(const String& moduleIp, bool power);

// Sent to a newly-connected dashboard client so it doesn't wait for the next change.
void broadcastAllStates();

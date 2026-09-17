#include "health.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

static const uint32_t WDT_TIMEOUT_S = 20;

static unsigned long disconnectedSince = 0;
static bool reconnectAttempted = false;
static const unsigned long WIFI_RECONNECT_AFTER_MS = 10000;
static const unsigned long WIFI_RESTART_AFTER_MS = 120000;

void healthInitWatchdog() {
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
}

void healthFeedWatchdog() {
  esp_task_wdt_reset();
}

void healthSuspendWatchdogForLongOp() {
  esp_task_wdt_delete(NULL);
}

void healthResumeWatchdogAfterLongOp() {
  esp_task_wdt_add(NULL);
}

void healthWatchWifi() {
  if (WiFi.getMode() != WIFI_STA) return;

  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince = 0;
    reconnectAttempted = false;
    return;
  }

  if (disconnectedSince == 0) {
    disconnectedSince = millis();
    return;
  }

  unsigned long down = millis() - disconnectedSince;
  if (!reconnectAttempted && down > WIFI_RECONNECT_AFTER_MS) {
    Serial.println("[Health] WiFi down, asking radio to reconnect");
    WiFi.reconnect();
    reconnectAttempted = true;
  }

  if (down > WIFI_RESTART_AFTER_MS) {
    Serial.println("[Health] WiFi still down after reconnect attempt, restarting");
    ESP.restart();
  }
}

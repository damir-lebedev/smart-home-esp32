#include "health.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>

static const uint32_t WDT_TIMEOUT_S = 20;

static unsigned long disconnectedSince = 0;
static bool reconnectAttempted = false;
static const unsigned long WIFI_RECONNECT_AFTER_MS = 10000;
static const unsigned long WIFI_RESTART_AFTER_MS = 120000;

void healthInitWatchdog() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  esp_task_wdt_config_t config = {
      .timeout_ms = WDT_TIMEOUT_S * 1000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  esp_task_wdt_init(&config);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
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

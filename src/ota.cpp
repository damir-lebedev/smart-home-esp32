#include "ota.h"
#include "globals.h"
#include "health.h"

#include <LittleFS.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <vector>

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>

// ── Self-update (runs on every module, including the master updating itself) ──

static bool pendingOtaRequested = false;
static String pendingOtaUrl;

static void scheduleSelfUpdate(const String& url) {
  pendingOtaUrl = url;
  pendingOtaRequested = true;
}

static void applyPendingSelfUpdate() {
  Serial.println("[OTA] Pulling firmware from " + pendingOtaUrl);
  WiFiClient client;
  httpUpdate.rebootOnUpdate(false);

  // This blocks for as long as the download + flash write takes (can be a
  // minute or more on a weak WiFi link), well past the watchdog timeout.
  healthSuspendWatchdogForLongOp();
  t_httpUpdate_return ret = httpUpdate.update(client, pendingOtaUrl);
  healthResumeWatchdogAfterLongOp();

  switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println("[OTA] Update OK, rebooting");
      delay(300);
      ESP.restart();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] No update applied");
      break;
    case HTTP_UPDATE_FAILED:
    default:
      Serial.printf("[OTA] Failed (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      break;
  }
}

// ── Master: stores the uploaded firmware and serves it to the network ──

static const char* FIRMWARE_PATH = "/firmware.bin";
static File otaUploadFile;
static bool otaUploadUnauthorized = false;
static bool otaUploadOpenFailed = false;
static size_t otaUploadBytesWritten = 0;
static bool fsMounted = false;

static void registerFirmwareUploadRoute() {
  server.on(
    "/fleet/upload", HTTP_POST,
    [](AsyncWebServerRequest* r) {
      if (otaUploadUnauthorized) {
        otaUploadUnauthorized = false;
        r->send(401, "text/plain; charset=utf-8", "Unauthorized");
        return;
      }
      if (otaUploadOpenFailed) {
        r->send(
          500, "text/plain; charset=utf-8",
          String("Could not open /firmware.bin for writing (LittleFS mounted: ") + (fsMounted ? "yes" : "no") + ")"
        );
        return;
      }
      r->send(200, "text/plain; charset=utf-8", "Uploaded: " + String((unsigned)otaUploadBytesWritten) + " bytes");
    },
    [](AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      if (index == 0) {
        otaUploadBytesWritten = 0;
        otaUploadOpenFailed = false;
        if (!adminKeyOk(r)) {
          otaUploadUnauthorized = true;
          return;
        }
        otaUploadUnauthorized = false;
        Serial.println("[OTA] Receiving firmware: " + filename);
        if (LittleFS.exists(FIRMWARE_PATH)) LittleFS.remove(FIRMWARE_PATH);
        otaUploadFile = LittleFS.open(FIRMWARE_PATH, "w");
        if (!otaUploadFile) {
          otaUploadOpenFailed = true;
          Serial.println("[OTA] Failed to open /firmware.bin for writing");
        }
      }
      if (otaUploadUnauthorized || otaUploadOpenFailed) return;
      if (otaUploadFile) {
        otaUploadBytesWritten += otaUploadFile.write(data, len);
      }
      if (final) {
        if (otaUploadFile) otaUploadFile.close();
        Serial.printf("[OTA] Firmware stored: %u bytes\n", (unsigned)otaUploadBytesWritten);
      }
    }
  );
}

static void registerFirmwareDebugRoute() {
  server.on("/fleet/debug", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!adminKeyOk(r)) {
      r->send(401, "text/plain; charset=utf-8", "Unauthorized");
      return;
    }
    String out;
    out += "mounted=" + String(fsMounted ? "yes" : "no") + "\n";
    out += "total=" + String((unsigned)LittleFS.totalBytes()) + "\n";
    out += "used=" + String((unsigned)LittleFS.usedBytes()) + "\n";
    out += "exists(/firmware.bin)=" + String(LittleFS.exists(FIRMWARE_PATH) ? "yes" : "no") + "\n";
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while (f) {
      out += String("entry: ") + f.path() + " (" + (unsigned)f.size() + " bytes)\n";
      f = root.openNextFile();
    }
    r->send(200, "text/plain; charset=utf-8", out);
  });
}

static void registerFirmwareServeRoute() {
  server.on("/firmware.bin", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!adminKeyOk(r)) {
      r->send(401, "text/plain; charset=utf-8", "Unauthorized");
      return;
    }
    if (!LittleFS.exists(FIRMWARE_PATH)) {
      r->send(404, "text/plain; charset=utf-8", "No firmware uploaded yet");
      return;
    }
    r->send(LittleFS, FIRMWARE_PATH, "application/octet-stream");
  });
}

// ── Master: sequential rollout across discovered modules ──

enum class OtaState { PENDING, UPDATING, DONE, TIMEOUT, UNREACHABLE };

struct OtaTarget {
  String ip;
  String name;
  OtaState state;
  String oldFw;
  unsigned long startedAt;
};

static std::vector<OtaTarget> otaTargets;
static bool otaDeployActive = false;
static int otaCurrentIndex = -1;
static unsigned long otaLastTick = 0;
static const unsigned long OTA_TICK_INTERVAL = 1500;
static const unsigned long OTA_STEP_TIMEOUT = 90000;

static const char* otaStateName(OtaState s) {
  switch (s) {
    case OtaState::PENDING: return "ожидание";
    case OtaState::UPDATING: return "обновляется";
    case OtaState::DONE: return "готово";
    case OtaState::TIMEOUT: return "не ответил (таймаут)";
    case OtaState::UNREACHABLE: return "недоступен";
  }
  return "?";
}

static String fetchFwVersion(const String& ip) {
  HTTPClient http;
  http.setTimeout(1500);
  if (!http.begin("http://" + ip + "/status")) return "";
  int code = http.GET();
  String fw;
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      fw = doc["fw"] | "";
    }
  }
  http.end();
  return fw;
}

static void triggerPull(const String& targetIp) {
  String key = otaKey();
  String firmwareUrl = "http://" + WiFi.localIP().toString() + "/firmware.bin";
  if (key.length()) firmwareUrl += "?key=" + urlEncode(key);

  if (targetIp == WiFi.localIP().toString()) {
    scheduleSelfUpdate(firmwareUrl);
    return;
  }

  String pullUrl = "http://" + targetIp + "/fleet/pull?src=" + urlEncode(firmwareUrl);
  if (key.length()) pullUrl += "&key=" + urlEncode(key);

  HTTPClient http;
  if (!http.begin(pullUrl)) return;
  http.setTimeout(3000);
  http.GET();
  http.end();
}

static void beginTarget(OtaTarget& t) {
  Serial.println("[OTA] Deploying to " + t.name + " (" + t.ip + ")");
  t.oldFw = fetchFwVersion(t.ip);
  t.state = OtaState::UPDATING;
  t.startedAt = millis();
  triggerPull(t.ip);
}

static void advanceToNextTarget() {
  otaCurrentIndex++;
  if (otaCurrentIndex >= (int)otaTargets.size()) {
    otaDeployActive = false;
    Serial.println("[OTA] Network rollout finished");
    return;
  }
  beginTarget(otaTargets[otaCurrentIndex]);
}

static void masterDeployTick() {
  if (!otaDeployActive) return;
  if (millis() - otaLastTick < OTA_TICK_INTERVAL) return;
  otaLastTick = millis();

  if (otaCurrentIndex == -1) {
    if (otaTargets.empty()) {
      otaDeployActive = false;
      return;
    }
    otaCurrentIndex = 0;
    beginTarget(otaTargets[0]);
    return;
  }

  OtaTarget& t = otaTargets[otaCurrentIndex];
  if (t.state == OtaState::UPDATING) {
    String fw = fetchFwVersion(t.ip);
    if (fw.length() && fw != t.oldFw) {
      t.state = OtaState::DONE;
    } else if (millis() - t.startedAt > OTA_STEP_TIMEOUT) {
      t.state = t.oldFw.length() ? OtaState::TIMEOUT : OtaState::UNREACHABLE;
    } else {
      return;  // still waiting on this target
    }
  }

  advanceToNextTarget();
}

static void startDeploy(bool includeSelf) {
  otaTargets.clear();
  for (auto& entry : discoveredModules) {
    OtaTarget t;
    t.ip = entry.first;
    t.name = entry.second.name;
    t.state = OtaState::PENDING;
    otaTargets.push_back(t);
  }
  if (includeSelf) {
    // Flash the master last: if it reboots mid-rollout it would lose the
    // in-RAM progress for every target still waiting.
    OtaTarget self;
    self.ip = WiFi.localIP().toString();
    self.name = deviceName;
    self.state = OtaState::PENDING;
    otaTargets.push_back(self);
  }
  otaDeployActive = true;
  otaCurrentIndex = -1;
  otaLastTick = 0;
}

static void registerDeployRoutes() {
  server.on("/fleet/deploy", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!adminKeyOk(r)) {
      r->send(401, "text/plain; charset=utf-8", "Unauthorized");
      return;
    }
    if (!LittleFS.exists(FIRMWARE_PATH)) {
      r->send(400, "text/plain; charset=utf-8", "No firmware uploaded");
      return;
    }
    if (otaDeployActive) {
      r->send(409, "text/plain; charset=utf-8", "Deploy already running");
      return;
    }
    bool includeSelf = true;
    if (r->hasParam("includeSelf")) includeSelf = r->getParam("includeSelf")->value() != "0";
    startDeploy(includeSelf);
    r->send(200, "text/plain; charset=utf-8", "Deploy started");
  });

  server.on("/fleet/deploy/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument doc;
    doc["active"] = otaDeployActive;
    JsonArray arr = doc["targets"].to<JsonArray>();
    for (auto& t : otaTargets) {
      JsonObject o = arr.add<JsonObject>();
      o["ip"] = t.ip;
      o["name"] = t.name;
      o["state"] = otaStateName(t.state);
    }
    String buf;
    serializeJson(doc, buf);
    r->send(200, "application/json", buf);
  });
}

// ── Public API ──

void otaBegin() {
  if (otaKey().length()) {
    ElegantOTA.setAuth("admin", otaKey().c_str());
  }
  ElegantOTA.begin(&server);

  server.on("/fleet/pull", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!adminKeyOk(r)) {
      r->send(401, "text/plain; charset=utf-8", "Unauthorized");
      return;
    }
    if (!r->hasParam("src")) {
      r->send(400, "text/plain; charset=utf-8", "Missing src");
      return;
    }
    scheduleSelfUpdate(r->getParam("src")->value());
    r->send(200, "text/plain; charset=utf-8", "Scheduled");
  });

  if (role == "master") {
    fsMounted = LittleFS.begin(true);
    if (!fsMounted) {
      Serial.println("[OTA] LittleFS mount failed");
    }
    registerFirmwareUploadRoute();
    registerFirmwareServeRoute();
    registerFirmwareDebugRoute();
    registerDeployRoutes();
  }
}

void otaLoop() {
  ElegantOTA.loop();

  if (pendingOtaRequested) {
    pendingOtaRequested = false;
    applyPendingSelfUpdate();
  }

  if (role == "master") {
    masterDeployTick();
  }
}

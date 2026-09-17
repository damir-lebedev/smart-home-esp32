#include "net_setup.h"
#include "globals.h"
#include "ws_events.h"
#include "health.h"

static String renderConfigPage() {
  String checkedSlave = role == "master" ? "" : "checked";
  String checkedMaster = role == "master" ? "checked" : "";
  String currentType = prefs.getString("type", "relay");
  String checkedRelay = currentType == "rgb" ? "" : "checked";
  String checkedRgb = currentType == "rgb" ? "checked" : "";

  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Настройка SmartModule</title>
  <style>
    body{font-family:system-ui,sans-serif; max-width:640px; margin:20px auto; padding:16px; background:#f9f9f9;}
    h1{text-align:center; color:#1a73e8;}
    label{display:block; margin:12px 0 4px; font-weight:500;}
    input, button{width:100%; padding:12px; margin:6px 0; border-radius:8px; border:1px solid #ccc; box-sizing:border-box; font-size:16px;}
    button{background:#1a73e8; color:white; border:none; cursor:pointer;}
    button:hover{background:#1557b0;}
    .radio-group{margin:12px 0;}
    .hint{color:#5f6368;font-size:13px;margin:-2px 0 0;}
  </style>
</head>
<body>
<h1>Настройка модуля</h1>

<form id="setupForm" action="{{action}}" method="POST">
  <label>Wi-Fi SSID</label>
  <input type="text" name="ssid" value="{{ssid}}" required>

  <label>Пароль Wi-Fi</label>
  <input type="password" name="pass" placeholder="оставьте пустым, чтобы не менять">

  <label>Имя модуля (комната)</label>
  <input type="text" name="name" value="{{name}}" required>

  <div class="radio-group">
    <label><input type="radio" name="role" value="slave" {{checkedSlave}}> Обычный модуль (slave)</label><br>
    <label><input type="radio" name="role" value="master" {{checkedMaster}}> Главный модуль (master)</label>
  </div>

  <div class="radio-group">
    <label><input type="radio" name="type" value="relay" {{checkedRelay}}> Реле (свет/розетка вкл-выкл)</label><br>
    <label><input type="radio" name="type" value="rgb" {{checkedRgb}}> RGB-подсветка (лента WS2812)</label>
  </div>

  <div class="radio-group">
    <label><input type="checkbox" name="invert" value="1" {{checkedInvert}}> Реле инвертировано (другая ревизия платы — включается высоким уровнем, а не низким; не относится к RGB-модулю)</label>
  </div>

  <label>Пин данных RGB-ленты (GPIO, только для RGB-модуля)</label>
  <input type="number" name="ledpin" min="0" max="21" value="{{ledpin}}">

  <label>Ключ OTA (общий для всей сети, необязательно)</label>
  <input type="text" name="otakey" placeholder="оставьте пустым, чтобы не менять" value="{{keyfield}}">
  <p class="hint">Один и тот же ключ нужно задать на всех модулях, иначе мастер не сможет их обновить.{{keyhint}}</p>

  {{adminkey}}

  <button type="submit" style="margin-top:24px; font-size:18px; padding:14px;">Сохранить и перезагрузить</button>
</form>
</body>
</html>
)rawliteral";

  page.replace("{{ssid}}", prefs.getString("ssid", ""));
  page.replace("{{name}}", deviceName);
  page.replace("{{checkedSlave}}", checkedSlave);
  page.replace("{{checkedMaster}}", checkedMaster);
  page.replace("{{checkedRelay}}", checkedRelay);
  page.replace("{{checkedRgb}}", checkedRgb);
  page.replace("{{checkedInvert}}", prefs.getBool("invert", false) ? "checked" : "");
  page.replace("{{ledpin}}", String(prefs.getUChar("ledpin", 0)));
  page.replace("{{keyfield}}", "");
  return page;
}

static void handleConfigForm(AsyncWebServerRequest* r, const String& action) {
  String page = renderConfigPage();
  page.replace("{{action}}", action);
  bool needsKey = otaKey().length() > 0 && action == "/config";
  page.replace(
    "{{keyhint}}",
    needsKey ? " На этом модуле ключ уже задан — понадобится ввести его ниже, чтобы сохранить изменения." : ""
  );
  page.replace(
    "{{adminkey}}",
    needsKey
      ? "<label>Текущий ключ OTA (для подтверждения)</label><input type=\"text\" name=\"key\" required>"
      : ""
  );
  r->send(200, "text/html; charset=utf-8", page);
}

static unsigned long restartAtMs = 0;

static void handleConfigSave(AsyncWebServerRequest* r) {
  if (!adminKeyOk(r)) {
    r->send(401, "text/plain; charset=utf-8", "Неверный ключ OTA");
    return;
  }

  String ssid, pass, name, roleVal, typeVal, otakey, ledpinStr;
  if (r->hasParam("ssid", true)) ssid = r->getParam("ssid", true)->value();
  if (r->hasParam("pass", true)) pass = r->getParam("pass", true)->value();
  if (r->hasParam("name", true)) name = r->getParam("name", true)->value();
  if (r->hasParam("role", true)) roleVal = r->getParam("role", true)->value();
  if (r->hasParam("type", true)) typeVal = r->getParam("type", true)->value();
  if (r->hasParam("otakey", true)) otakey = r->getParam("otakey", true)->value();
  if (r->hasParam("ledpin", true)) ledpinStr = r->getParam("ledpin", true)->value();
  bool invert = r->hasParam("invert", true);

  if (ssid == "") {
    r->send(400, "text/plain; charset=utf-8", "SSID обязателен");
    return;
  }

  // Blank password/key fields mean "keep the current value", so renaming a
  // module doesn't force retyping the WiFi password every time.
  if (pass.length() == 0) pass = prefs.getString("pass", "");
  if (otakey.length() == 0) otakey = otaKey();
  uint8_t ledpin = ledpinStr.length() ? (uint8_t)ledpinStr.toInt() : prefs.getUChar("ledpin", 0);

  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("name", name.length() > 0 ? name : "Module");
  prefs.putString("role", roleVal == "master" ? "master" : "slave");
  prefs.putString("type", typeVal == "rgb" ? "rgb" : "relay");
  prefs.putString("otakey", otakey);
  prefs.putBool("invert", invert);
  prefs.putUChar("ledpin", ledpin);

  r->send(200, "text/plain; charset=utf-8", "Сохранено. Перезагрузка...");
  // Restart from loop() shortly after, not here: this handler runs on the
  // AsyncTCP task, and blocking it with delay()+restart() can tear the
  // connection down before the response actually reaches the client.
  restartAtMs = millis() + 800;
}

void netTick() {
  if (restartAtMs && millis() >= restartAtMs) {
    ESP.restart();
  }
}

// Available in every mode: lets you rename/re-role/re-key an already
// configured module without physical USB access.
void netRegisterConfigRoutes() {
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest* r) {
    handleConfigForm(r, "/config");
  });
  server.on("/config", HTTP_POST, handleConfigSave);
}

void netStartApMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, apSubnet);
  WiFi.softAP(apSSID);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    handleConfigForm(r, "/save");
  });
  server.on("/save", HTTP_POST, handleConfigSave);
  netRegisterConfigRoutes();

  ws.onEvent(wsOnEvent);
  server.addHandler(&ws);

  server.begin();
}

bool netConnectSTA(const String& ssid, const String& pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.setSleep(false);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 14000) {
    delay(400);
    healthFeedWatchdog();  // this runs before loop() starts feeding it
  }
  return WiFi.status() == WL_CONNECTED;
}

void netComputeBroadcastIP() {
  broadcastIP = WiFi.localIP();
  IPAddress subnet = WiFi.subnetMask();
  for (int i = 0; i < 4; i++) {
    broadcastIP[i] = (broadcastIP[i] & subnet[i]) | (~subnet[i]);
  }
}

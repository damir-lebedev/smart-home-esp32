#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>  // для UDP
#include <map>       // для std::map
#include <utility>   // для std::pair 
#include <vector>    
#include <AsyncWebSocket.h>

void broadcastRelayState(const String& moduleIp, bool power);
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
void broadcastAllStates();

IPAddress broadcastIP;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");           // endpoint будет ws://IP/ws
Preferences prefs;
WiFiUDP udp;

const int RELAY_PIN = 1;
bool relayState = false;

String deviceName = "Module";
String role = "slave";           // "slave" или "master"
String moduleType = "relay";     // пока жёстко, в будущем будет настройка

// Для master — обнаруженные модули: ip -> {name, lastSeen millis}
struct ModuleInfo {
  String name;
  String type;
  unsigned long lastSeen;
};

std::map<String, ModuleInfo> discoveredModules;

const int UDP_PORT = 12345;
unsigned long lastAnnounce = 0;
const unsigned long ANNOUNCE_INTERVAL = 5000;  // 5 сек
const unsigned long TIMEOUT = 15000;           // 15 сек без анонса — удалить

const char* apSSID = "SmartModule_Setup";
const IPAddress apIP(192, 168, 4, 1);
const IPAddress apSubnet(255, 255, 255, 0);

// ────────────────────────────────────────────────
// HTML-фрагмент для /ui  (для relay)
// ────────────────────────────────────────────────
String getUiFragment() {
  return R"rawliteral(
<div class="module-card">
  <h3>{{name}}</h3>
  <div class="status" id="status_{{safeid}}">Загрузка...</div>
  <div class="indicator" id="indicator_{{safeid}}">⚪</div>
  <div class="buttons">
    <button class="btn on"    onclick="cmd('{{ip}}','on')">ВКЛ</button>
    <button class="btn off"   onclick="cmd('{{ip}}','off')">ВЫКЛ</button>
    <button class="btn toggle" onclick="cmd('{{ip}}','toggle')">ПЕРЕКЛ</button>
  </div>
</div>
)rawliteral";
}

// ────────────────────────────────────────────────
// Страница настройки (AP mode)
// ────────────────────────────────────────────────
const char* setupPage = R"rawliteral(
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
  </style>
</head>
<body>
<h1>Настройка модуля</h1>

<form id="setupForm" action="/save" method="POST">
  <label>Wi-Fi SSID</label>
  <input type="text" name="ssid" required>

  <label>Пароль Wi-Fi</label>
  <input type="password" name="pass">

  <label>Имя модуля (комната)</label>
  <input type="text" name="name" value="Комната" required>

  <div class="radio-group">
    <label><input type="radio" name="role" value="slave" checked> Обычный модуль (slave)</label><br>
    <label><input type="radio" name="role" value="master"> Главный модуль (master)</label>
  </div>

  <button type="submit" style="margin-top:24px; font-size:18px; padding:14px;">Сохранить и перезагрузить</button>
</form>
</body>
</html>
)rawliteral";

// ────────────────────────────────────────────────
// SETUP
// ────────────────────────────────────────────────
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // начальное состояние

  prefs.begin("smartmod", false);

  // Читаем сохранённые значения
  String savedSsid    = prefs.getString("ssid", "");
  String savedPass    = prefs.getString("pass", "");
  deviceName          = prefs.getString("name", "Module");
  role                = prefs.getString("role", "slave");
  relayState          = prefs.getBool("relay", false);
  digitalWrite(RELAY_PIN, relayState ? LOW : HIGH);

  if (savedSsid == "") {
    // ─── AP режим ─────────────────────────────────────
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, apSubnet);
    WiFi.softAP(apSSID);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
      r->send(200, "text/html", setupPage);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *r){
      String ssid, pass, name, roleVal;

      if (r->hasParam("ssid", true))    ssid       = r->getParam("ssid", true)->value();
      if (r->hasParam("pass", true))    pass       = r->getParam("pass", true)->value();
      if (r->hasParam("name", true))    name       = r->getParam("name", true)->value();
      if (r->hasParam("role", true))    roleVal    = r->getParam("role", true)->value();

      if (ssid == "") {
        r->send(400, "text/plain", "SSID обязателен");
        return;
      }

      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      prefs.putString("name", name.length() > 0 ? name : "Module");
      prefs.putString("role", roleVal == "master" ? "master" : "slave");

      r->send(200, "text/plain", "Сохранено. Перезагрузка...");
      delay(1200);
      ESP.restart();
    });
    // ─── WebSocket ────────────────────────────────────────
    ws.onEvent(onWsEvent);              // обработчик событий
    server.addHandler(&ws);             // регистрируем WebSocket

    server.begin();
    return;
  }

  // ─── STA режим ──────────────────────────────────────
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  WiFi.setSleep(false);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 14000) {
    delay(400);
  }

  if (WiFi.status() != WL_CONNECTED) {
    prefs.clear();
    ESP.restart();
  }
  Serial.begin(115200);
  Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
  Serial.println("Subnet: " + WiFi.subnetMask().toString());

  // Вычисляем правильный broadcast-адрес подсети
  broadcastIP = WiFi.localIP();
  IPAddress subnet = WiFi.subnetMask();
  for (int i = 0; i < 4; i++) {
    broadcastIP[i] = (broadcastIP[i] & subnet[i]) | (~subnet[i]);
  }
  Serial.print("Calculated broadcast IP: ");
  Serial.println(broadcastIP);

  // Запускаем UDP
  udp.begin(UDP_PORT);

  // ─── Обязательные API для ВСЕХ модулей ──────────────
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r){
    DynamicJsonDocument doc(512);
    doc["api"]   = 1;
    doc["type"]  = moduleType;
    doc["name"]  = deviceName;

    JsonObject state = doc.createNestedObject("state");
    state["power"] = relayState;

    String buf;
    serializeJson(doc, buf);

    AsyncWebServerResponse *resp =
      r->beginResponse(200, "application/json", buf);

    resp->addHeader("Access-Control-Allow-Origin", "*");
    r->send(resp);
  });

  server.on("/ui", HTTP_GET, [](AsyncWebServerRequest *r){
    String fragment = getUiFragment();
    String ipStr = WiFi.localIP().toString();
    String safeId = ipStr;
    safeId.replace(".", "_");

    fragment.replace("{{name}}",   deviceName);
    fragment.replace("{{ip}}",     ipStr);
    fragment.replace("{{safeid}}", safeId);

    AsyncWebServerResponse *resp =
      r->beginResponse(200, "text/html", fragment);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    r->send(resp);
  });

  // ─── Управление реле (для типа relay) ───────────────
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *r){
    relayState = true;
    digitalWrite(RELAY_PIN, LOW);
    prefs.putBool("relay", true);
    broadcastRelayState(WiFi.localIP().toString(), relayState);
    r->send(200, "text/plain", "ON");
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *r){
    relayState = false;
    digitalWrite(RELAY_PIN, HIGH);
    prefs.putBool("relay", false);
    broadcastRelayState(WiFi.localIP().toString(), relayState);
    r->send(200, "text/plain", "OFF");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *r){
    relayState = !relayState;
    digitalWrite(RELAY_PIN, relayState ? LOW : HIGH);
    prefs.putBool("relay", relayState);
    broadcastRelayState(WiFi.localIP().toString(), relayState);
    r->send(200, "text/plain", relayState ? "ON" : "OFF");
  });

  // ─── Главная страница и API только для master ─────────────
  if (role == "master") {
    // Новый эндпоинт /modules — возвращает JSON списка обнаруженных
    server.on("/modules", HTTP_GET, [](AsyncWebServerRequest *r){
      DynamicJsonDocument doc(1024);
      JsonArray arr = doc.createNestedArray("modules");

      // Сам master
      JsonObject own = arr.createNestedObject();
      own["name"] = deviceName;
      own["ip"] = WiFi.localIP().toString();
      own["type"] = moduleType;
      own["isOwn"] = true;

      // Обнаруженные
      for (auto& entry : discoveredModules) {
  JsonObject mod = arr.createNestedObject();
  mod["name"] = entry.second.name;
  mod["ip"]   = entry.first;
  mod["type"] = entry.second.type;
}

      String buf;
      serializeJson(doc, buf);
      r->send(200, "application/json", buf);
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
      String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SmartModule — Управление</title>
<style>
  :root{--bg:#f0f2f5;--card:#fff;--pri:#1a73e8;--on:#34a853;--off:#ea4335;--gray:#5f6368;}
  body{font-family:system-ui,sans-serif;background:var(--bg);margin:0;padding:20px;color:#202124;}
  h1{text-align:center;margin-bottom:32px;}
  .toolbar{text-align:center;margin-bottom:32px;}
  .big-btn{padding:14px 32px;font-size:17px;border:none;border-radius:10px;color:white;cursor:pointer;margin:0 12px;}
  .all-on{background:var(--on);}
  .all-off{background:var(--off);}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:20px;}
  .module-card{background:var(--card);border-radius:16px;padding:20px;box-shadow:0 2px 10px rgba(0,0,0,0.08);text-align:center;}
  .module-card h3{margin:0 0 12px;font-size:1.4em;}
  .status{font-size:1.3em;font-weight:600;margin:12px 0;}
  .indicator{font-size:3.8em;margin:8px 0;min-height:1.2em;}
  .buttons{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;}
  .btn{padding:10px 18px;font-size:15px;border:none;border-radius:8px;color:white;cursor:pointer;min-width:80px;}
  .on{background:var(--on);}
  .off{background:var(--off);}
  .toggle{background:var(--pri);}
  .offline .status{color:#d93025 !important;}
  .offline .indicator{opacity:0.4;}
</style>
</head>
<body>
<h1>Управление домом</h1>

<div class="toolbar">
  <button class="big-btn all-on" onclick="allCmd('on')">ВКЛ ВСЁ</button>
  <button class="big-btn all-off" onclick="allCmd('off')">ВЫКЛ ВСЁ</button>
</div>

<div class="grid" id="grid"></div>

<script>
let modules = [];

function safeId(ip) {
  return ip.replace(/\./g, '_');
}

function cmd(ip, act) {
  fetch(`http://${ip}/${act}`).then(() => {
    // сразу после команды обновляем этот модуль
    updateStatus({ip: ip});
  }).catch(e => console.log("cmd error", e));
}

function allCmd(act) {
  modules.forEach(m => cmd(m.ip, act));
}

async function loadModuleCard(m) {
  const sid = safeId(m.ip);
  const existingCard = document.getElementById('card_' + sid);
  if (existingCard) return; 
  
  const grid = document.getElementById('grid');
  const div = document.createElement('div');
  div.className = 'module-card';
  div.id = 'card_' + sid;

  try {
    const resp = await fetch(`http://${m.ip}/ui`);
    let htmlFrag = await resp.text();

    htmlFrag = htmlFrag.replace(/{{ip}}/g, m.ip);
    htmlFrag = htmlFrag.replace(/{{safeid}}/g, sid);
    htmlFrag = htmlFrag.replace(/{{name}}/g, m.name);

    div.innerHTML = htmlFrag;
  } catch(e) {
    div.innerHTML = `<h3>${m.name}</h3><div class="status">Нет связи</div><div class="indicator">❌</div>`;
    div.classList.add('offline');
  }

  grid.appendChild(div);
  updateStatus(m);
}

async function updateStatus(m) {
  const sid = safeId(m.ip);
  const card = document.getElementById('card_' + sid);
  if (!card) return;

  try {
    const r = await fetch(`http://${m.ip}/status`);
    const d = await r.json();
    const power = !!d.state?.power;

    const st = card.querySelector('.status');
    const ind = card.querySelector('.indicator');

    if (st && ind) {
      st.textContent = power ? 'ВКЛЮЧЕНО' : 'ВЫКЛЮЧЕНО';
      st.style.color = power ? 'var(--on)' : 'var(--off)';
      ind.textContent = power ? '💡' : '⚪';
      ind.style.color = power ? 'var(--on)' : 'var(--gray)';
      card.classList.remove('offline');
    }
  } catch(e) {
    const st = card.querySelector('.status');
    const ind = card.querySelector('.indicator');
    if (st) st.textContent = 'Нет связи';
    if (ind) ind.textContent = '❌';
    card.classList.add('offline');
  }
}

async function refreshModules() {
  try {
    const resp = await fetch('/modules');
    const data = await resp.json();
    modules = data.modules;

    // Удаляем устаревшие карточки
    const grid = document.getElementById('grid');
    const currentIds = new Set(modules.map(m => safeId(m.ip)));
    Array.from(grid.children).forEach(child => {
      const sid = child.id.replace('card_', '');
      if (!currentIds.has(sid)) grid.removeChild(child);
    });

    // Добавляем/обновляем
    for (const m of modules) {
      await loadModuleCard(m);
      updateStatus(m);
    }
  } catch(e) {
    console.log('Ошибка загрузки modules');
  }
}

let ws = null;

function connectWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = protocol + '//' + window.location.host + '/ws';
  
  ws = new WebSocket(wsUrl);

  ws.onopen = () => {
    console.log('WebSocket connected');
  };

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      if (data.event === 'state') {
        const sid = safeId(data.ip);
        const card = document.getElementById('card_' + sid);
        if (!card) return;

        const st = card.querySelector('.status');
        const ind = card.querySelector('.indicator');

        if (st && ind) {
          const power = data.power;
          st.textContent = power ? 'ВКЛЮЧЕНО' : 'ВЫКЛЮЧЕНО';
          st.style.color = power ? 'var(--on)' : 'var(--off)';
          ind.textContent = power ? '💡' : '⚪';
          ind.style.color = power ? 'var(--on)' : 'var(--gray)';
          card.classList.remove('offline');
        }
      }
    } catch(e) {
      console.error('WS parse error', e);
    }
  };

  ws.onclose = () => {
    console.log('WebSocket disconnected — reconnecting...');
    setTimeout(connectWebSocket, 2000);
  };

  ws.onerror = (err) => {
    console.error('WebSocket error', err);
    ws.close();
  };
}

// Запускаем
connectWebSocket();

// refreshModules() можно оставить как fallback каждые 30–60 сек
setInterval(refreshModules, 60000);  // Каждые 10 сек
refreshModules();  // Инициализация
</script>
</body>
</html>
)rawliteral";

      r->send(200, "text/html", html);
    });
  }

  // Для slave заглушка
  else {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
      r->send(200, "text/plain", "Это slave-модуль. Используйте /ui, /status, /on, /off, /toggle");
    });
  }
  

  server.begin();
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
}

void loop() {
  if (role == "slave") {
    if (millis() - lastAnnounce >= ANNOUNCE_INTERVAL) {
      lastAnnounce = millis();
      String ip = WiFi.localIP().toString();
      DynamicJsonDocument doc(512);
doc["cmd"]  = "announce";
doc["name"] = deviceName;
doc["type"] = moduleType;
doc["ip"]   = WiFi.localIP().toString();

String packet;
serializeJson(doc, packet);

udp.beginPacket(broadcastIP, UDP_PORT);
udp.print(packet);
udp.endPacket();
      

    }
  } 
  else if (role == "master") {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char buf[256] = {0};  // +1 для \0
      int len = udp.read(buf, sizeof(buf) - 1);
      if (len > 0) {
        String msg = String(buf);
        msg.trim();  // убираем лишние пробелы / \r\n

        DynamicJsonDocument doc(256);
DeserializationError err = deserializeJson(doc, msg);

if (!err) {
  String cmd = doc["cmd"] | "";

  if (cmd == "announce") {

    String name = doc["name"] | "Unknown";
    String type = doc["type"] | "unknown";
    String ip   = doc["ip"]   | "";

    if (ip.length() > 6 && ip != WiFi.localIP().toString()) {

      ModuleInfo info;
      info.name = name;
      info.type = type;
      info.lastSeen = millis();

      discoveredModules[ip] = info;

      Serial.printf("Registered module: %s (%s) type=%s\n",
        name.c_str(),
        ip.c_str(),
        type.c_str()
      );
    }
  }
}
      }
    }

    // Чистка устаревших
    unsigned long now = millis();
    for (auto it = discoveredModules.begin(); it != discoveredModules.end(); ) {
      if (now - it->second.lastSeen > TIMEOUT) {
        Serial.println("Timeout removed: " + it->first);
        it = discoveredModules.erase(it);
      } else {
        ++it;
      }
    }
  }
}
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    // Можно сразу отправить текущее состояние всем модулям новому клиенту
    broadcastAllStates();
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
  // Можно добавить обработку сообщений от клиента, например "refresh"
}
void broadcastRelayState(const String& moduleIp, bool power) {
  if (ws.count() == 0) return;

  DynamicJsonDocument doc(256);
  doc["event"] = "state";
  doc["ip"]    = moduleIp;
  doc["power"] = power;

  String msg;
  serializeJson(doc, msg);

  ws.textAll(msg);   // рассылаем ВСЕМ подключённым клиентам
}

void broadcastAllStates() {
  // Своё состояние
  broadcastRelayState(WiFi.localIP().toString(), relayState);

  // Все обнаруженные модули (если есть способ узнать их состояние)
  // Пока просто placeholder — в идеале запрашивать /status у каждого, но это медленно
  // Поэтому лучше пусть каждый slave сам рассылает при изменении
}

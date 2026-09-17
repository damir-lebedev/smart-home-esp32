#include "web_ui.h"
#include "globals.h"
#include "discovery.h"
#include <ArduinoJson.h>

static String getRelayUiFragment() {
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

static String getRgbUiFragment() {
  return R"rawliteral(
<div class="module-card">
  <h3>🌈 {{name}}</h3>
  <div class="status" id="status_{{safeid}}">Загрузка...</div>
  <div class="indicator" id="indicator_{{safeid}}">⚪</div>
  <div class="buttons">
    <button class="btn on"  onclick="cmd('{{ip}}','on')">ВКЛ</button>
    <button class="btn off" onclick="cmd('{{ip}}','off')">ВЫКЛ</button>
  </div>
  <div style="margin-top:10px">
    <select onchange="fetch('http://{{ip}}/mode?set='+this.value)">
      <option value="1">🎨 Статичный</option>
      <option value="2">🌈 Радуга</option>
      <option value="3">🌈 Цикл</option>
      <option value="4">🌬️ Дыхание</option>
      <option value="5">🏃 Chase</option>
    </select>
  </div>
  <div style="margin-top:10px">
    <input type="color" onchange="fetch('http://{{ip}}/color?hex='+this.value.replace('#',''))">
  </div>
  <div style="margin-top:10px">
    <input type="range" min="5" max="255" oninput="fetch('http://{{ip}}/brightness?val='+this.value)">
  </div>
  <div style="margin-top:10px">
    <input type="range" min="0" max="255" oninput="fetch('http://{{ip}}/speed?val='+this.value)">
  </div>
</div>
)rawliteral";
}

void webUiRegisterCommonRoutes() {
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument doc;
    doc["api"] = 1;
    doc["type"] = moduleType;
    doc["name"] = deviceName;
    doc["role"] = role;
    doc["fw"] = FIRMWARE_VERSION;

    JsonObject state = doc["state"].to<JsonObject>();
    if (moduleType == "rgb") {
      state["power"] = rgbPower;
      state["mode"] = rgbMode;
      state["brightness"] = rgbBrightness;
      state["color"] = rgbColorHex;
      state["speed"] = rgbSpeed;
    } else {
      state["power"] = relayState;
    }

    String buf;
    serializeJson(doc, buf);

    AsyncWebServerResponse* resp = r->beginResponse(200, "application/json", buf);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    r->send(resp);
  });

  server.on("/ui", HTTP_GET, [](AsyncWebServerRequest* r) {
    String fragment = moduleType == "rgb" ? getRgbUiFragment() : getRelayUiFragment();
    String ipStr = WiFi.localIP().toString();
    String safeId = ipStr;
    safeId.replace(".", "_");

    fragment.replace("{{name}}", deviceName);
    fragment.replace("{{ip}}", ipStr);
    fragment.replace("{{safeid}}", safeId);

    AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", fragment);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    r->send(resp);
  });

  server.on("/master", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument doc;
    doc.to<JsonObject>();  // always emit {} rather than null when no master is known
    String ip, name;
    if (discoveryGetMaster(ip, name)) {
      doc["ip"] = ip;
      doc["name"] = name;
    }
    String buf;
    serializeJson(doc, buf);
    r->send(200, "application/json; charset=utf-8", buf);
  });
}

void webUiRegisterMasterRoutes() {
  server.on("/modules", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument doc;
    JsonArray arr = doc["modules"].to<JsonArray>();

    JsonObject own = arr.add<JsonObject>();
    own["name"] = deviceName;
    own["ip"] = WiFi.localIP().toString();
    own["type"] = moduleType;
    own["isOwn"] = true;

    for (auto& entry : discoveredModules) {
      JsonObject mod = arr.add<JsonObject>();
      mod["name"] = entry.second.name;
      mod["ip"] = entry.first;
      mod["type"] = entry.second.type;
    }

    String buf;
    serializeJson(doc, buf);
    r->send(200, "application/json", buf);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
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
  h2{font-size:1.2em;}
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
  .ota-panel{background:var(--card);border-radius:16px;padding:20px;box-shadow:0 2px 10px rgba(0,0,0,0.08);max-width:640px;margin:0 auto 32px;}
  .ota-panel input[type=file]{width:100%;margin:8px 0;}
  .ota-panel button{padding:10px 18px;border:none;border-radius:8px;background:var(--pri);color:white;cursor:pointer;margin:6px 6px 6px 0;}
  .ota-panel label{display:block;margin:10px 0;}
  #fwInfo, #deployStatus{font-size:14px;color:var(--gray);margin-top:8px;}
  .deploy-row{padding:4px 0;}
</style>
</head>
<body>
<h1>Управление домом</h1>

<div class="ota-panel">
  <h2>Обновление прошивки по сети</h2>
  <input type="file" id="fwFile" accept=".bin">
  <button onclick="uploadFirmware()">Загрузить .bin на мастер</button>
  <div id="fwInfo"></div>
  <label><input type="checkbox" id="includeSelf" checked> Обновить и этот модуль (мастер) — последним</label>
  <button onclick="deployFirmware()">Обновить всю сеть</button>
  <div id="deployStatus"></div>
</div>

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

    const grid = document.getElementById('grid');
    const currentIds = new Set(modules.map(m => safeId(m.ip)));
    Array.from(grid.children).forEach(child => {
      const sid = child.id.replace('card_', '');
      if (!currentIds.has(sid)) grid.removeChild(child);
    });

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

async function uploadFirmware() {
  const f = document.getElementById('fwFile').files[0];
  if (!f) { alert('Выберите .bin файл'); return; }
  const fd = new FormData();
  fd.append('firmware', f, f.name);
  const info = document.getElementById('fwInfo');
  info.textContent = 'Загрузка...';
  try {
    const r = await fetch('/fleet/upload', {method: 'POST', body: fd});
    info.textContent = r.ok
      ? `Прошивка загружена (${f.size} байт). Можно запускать обновление сети.`
      : `Ошибка загрузки: ${r.status}`;
  } catch(e) {
    info.textContent = 'Ошибка загрузки: ' + e;
  }
}

let deployPoll = null;

async function deployFirmware() {
  const includeSelf = document.getElementById('includeSelf').checked ? '1' : '0';
  try {
    const r = await fetch('/fleet/deploy?includeSelf=' + includeSelf, {method: 'POST'});
    if (!r.ok) {
      alert('Не удалось запустить обновление: ' + r.status + ' ' + await r.text());
      return;
    }
    pollDeployStatus();
  } catch(e) {
    alert('Не удалось запустить обновление: ' + e);
  }
}

function pollDeployStatus() {
  if (deployPoll) clearInterval(deployPoll);
  deployPoll = setInterval(async () => {
    try {
      const r = await fetch('/fleet/deploy/status');
      const d = await r.json();
      const box = document.getElementById('deployStatus');
      let html = '';
      for (const t of d.targets) {
        html += `<div class="deploy-row">${t.name} (${t.ip}): ${t.state}</div>`;
      }
      box.innerHTML = html || 'Нет активного обновления';
      if (!d.active) clearInterval(deployPoll);
    } catch(e) {
      console.log('Ошибка опроса статуса обновления');
    }
  }, 2000);
}

connectWebSocket();
setInterval(refreshModules, 60000);
refreshModules();
pollDeployStatus();
</script>
</body>
</html>
)rawliteral";

    r->send(200, "text/html; charset=utf-8", html);
  });
}

void webUiRegisterSlaveRoute() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    String moduleHtml = moduleType == "rgb" ? R"rawliteral(
<div class="module-card">
  <h3>🌈 {{name}}</h3>
  <div class="status" id="status">Загрузка...</div>
  <div class="indicator" id="indicator">⚪</div>
  <div class="buttons">
    <button class="btn on" onclick="cmd('on')">ВКЛ</button>
    <button class="btn off" onclick="cmd('off')">ВЫКЛ</button>
  </div>
  <div style="margin-top:10px">
    <select onchange="fetch('/mode?set='+this.value)">
      <option value="1">🎨 Статичный</option>
      <option value="2">🌈 Радуга</option>
      <option value="3">🌈 Цикл</option>
      <option value="4">🌬️ Дыхание</option>
      <option value="5">🏃 Chase</option>
    </select>
  </div>
  <div style="margin-top:10px">
    <input type="color" onchange="fetch('/color?hex='+this.value.replace('#',''))">
  </div>
  <div style="margin-top:10px">
    <input type="range" min="5" max="255" oninput="fetch('/brightness?val='+this.value)">
  </div>
  <div style="margin-top:10px">
    <input type="range" min="0" max="255" oninput="fetch('/speed?val='+this.value)">
  </div>
</div>
)rawliteral" : R"rawliteral(
<div class="module-card">
  <h3>{{name}}</h3>
  <div class="status" id="status">Загрузка...</div>
  <div class="indicator" id="indicator">⚪</div>
  <div class="buttons">
    <button class="btn on" onclick="cmd('on')">ВКЛ</button>
    <button class="btn off" onclick="cmd('off')">ВЫКЛ</button>
    <button class="btn toggle" onclick="cmd('toggle')">ПЕРЕКЛ</button>
  </div>
</div>
)rawliteral";

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{{name}} — SmartModule</title>
<style>
  :root{--bg:#f0f2f5;--card:#fff;--pri:#1a73e8;--on:#34a853;--off:#ea4335;--gray:#5f6368;}
  body{font-family:system-ui,sans-serif;background:var(--bg);margin:0;padding:20px;color:#202124;display:flex;flex-direction:column;align-items:center;}
  .module-card{background:var(--card);border-radius:16px;padding:28px;box-shadow:0 2px 10px rgba(0,0,0,0.08);text-align:center;max-width:360px;width:100%;margin-top:12vh;box-sizing:border-box;}
  .module-card h3{margin:0 0 12px;font-size:1.6em;}
  .status{font-size:1.3em;font-weight:600;margin:12px 0;}
  .indicator{font-size:4.5em;margin:8px 0;min-height:1.2em;}
  .buttons{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;}
  .btn{padding:10px 18px;font-size:15px;border:none;border-radius:8px;color:white;cursor:pointer;min-width:80px;}
  .on{background:var(--on);}
  .off{background:var(--off);}
  .toggle{background:var(--pri);}
  .master-link{margin-top:20px;text-align:center;}
  .master-link a{color:var(--pri);text-decoration:none;font-weight:600;font-size:1.05em;}
  .footer-link{margin-top:32px;font-size:13px;}
  .footer-link a{color:var(--gray);}
</style>
</head>
<body>
{{module}}
<div class="master-link" id="masterLink"></div>
<div class="footer-link"><a href="/config">Настройки модуля</a></div>

<script>
function cmd(act) {
  fetch('/' + act).then(refresh).catch(() => {});
}

async function refresh() {
  try {
    const r = await fetch('/status');
    const d = await r.json();
    const power = !!d.state?.power;
    const st = document.getElementById('status');
    const ind = document.getElementById('indicator');
    st.textContent = power ? 'ВКЛЮЧЕНО' : 'ВЫКЛЮЧЕНО';
    st.style.color = power ? 'var(--on)' : 'var(--off)';
    ind.textContent = power ? '💡' : '⚪';
  } catch (e) {}
}

async function loadMaster() {
  try {
    const r = await fetch('/master');
    const d = await r.json();
    const box = document.getElementById('masterLink');
    box.innerHTML = d.ip ? `<a href="http://${d.ip}/">🏠 К мастеру (${d.name})</a>` : '';
  } catch (e) {}
}

refresh();
loadMaster();
setInterval(refresh, 5000);
setInterval(loadMaster, 10000);
</script>
</body>
</html>
)rawliteral";
    html.replace("{{module}}", moduleHtml);
    html.replace("{{name}}", deviceName);
    r->send(200, "text/html; charset=utf-8", html);
  });
}

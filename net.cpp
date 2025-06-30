#include "net.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "storage.h"
#include "config.h"
#include "ads_driver.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

const char *SSID = "NEST-Shop";
const char *PASS = "NDA1T5D0";
static constexpr long TZ_OFFSET = -3 * 3600;

/* ----------------------------------------------------------
   Single‑page HTML: Monitor + Setup
   ---------------------------------------------------------- */
static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>LiPo Monitor</title>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<style>
body { font-family: 'Segoe UI', sans-serif; background: #f4f4f9; margin: 0 auto; max-width: 1000px; padding: 20px; color: #333; }
h2 { text-align: center; }
.tabs { text-align: center; margin-bottom: 20px; }
.tabs button {
  background-color: #2196F3;
  border: none;
  color: white;
  padding: 10px 20px;
  margin: 0 5px;
  border-radius: 6px;
  cursor: pointer;
  transition: background-color 0.3s;
}
.tabs button:hover { background-color: #1565c0; }
.tabs button.active { background-color: #0d47a1; }
.pane { display: none; }
.pane.active { display: block; }
.cards { display: flex; gap: 12px; flex-wrap: wrap; justify-content: center; }
.card { background: white; border-radius: 10px; padding: 12px; width: 140px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); text-align: center; cursor: pointer; transition: transform 0.2s; }
.card:hover { transform: scale(1.05); }
.battery { width: 50px; height: 100px; border: 3px solid #333; border-radius: 6px; position: relative; margin: 8px auto; background: linear-gradient(to top, var(--batt-color) var(--level), #ddd var(--level)); }
.battery::before { content: ""; position: absolute; top: -10px; left: 15px; width: 20px; height: 6px; background: #333; border-radius: 2px; }
.total-battery { border-width: 4px; }
canvas { max-width: 100%; height: 180px; margin-top: 20px; }
a { display: inline-block; margin-top: 12px; color: #2196F3; text-decoration: none; }
a:hover { text-decoration: underline; }
.setup-box { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); max-width: 500px; margin: auto; }
.setup-box h3 { margin-top: 0; }
.setup-box input { width: 60px; padding: 5px; margin: 5px; border: 1px solid #ccc; border-radius: 4px; }
.setup-box button { background-color: #2196F3; color: white; border: none; padding: 8px 16px; border-radius: 6px; margin: 8px 0; cursor: pointer; transition: background-color 0.3s; }
.setup-box button:hover { background-color: #0d47a1; }
</style>
</head>
<body>
<h2>Monitor de Baterias LiPo</h2>
<div class='tabs'>
 <button id='tabMon' class='active'>Monitor</button>
 <button id='tabSet'>Setup</button>
</div>
<div id='paneMon' class='pane active'>
  <div class='cards'>
    <div class='card' onclick="selectCell(0)">C1<div class='battery' id='batt0'></div><span id='v0'>-</span> V</div>
    <div class='card' onclick="selectCell(1)">C2<div class='battery' id='batt1'></div><span id='v1'>-</span> V</div>
    <div class='card' onclick="selectCell(2)">C3<div class='battery' id='batt2'></div><span id='v2'>-</span> V</div>
    <div class='card' onclick="selectCell(3)">C4<div class='battery' id='batt3'></div><span id='v3'>-</span> V</div>
    <div class='card' onclick="selectCell(4)">Total<div class='battery total-battery' id='battTot'></div><span id='tot'>-</span> V</div>
  </div>
  <canvas id='graph'></canvas>
  <a href='/download'>📥 Baixar CSV</a>
</div>
<div id='paneSet' class='pane'>
  <div class="setup-box">
    <h3>⚙️ Calibração kDiv</h3>
    <p>Digite tensões medidas (V):</p>
    <div>🔋 C1 <input id='in0'> V</div>
    <div>🔋 C1+C2 <input id='in1'> V</div>
    <div>🔋 C1+C2+C3 <input id='in2'> V</div>
    <div>🔋 Pack <input id='in3'> V</div>
    <button onclick='calib()'>✅ Calibrar</button>
    <h3>🗑️ Logs</h3>
    <button onclick='clearLog()'>🧹 Limpar logs</button>
  </div>
</div>
<script>
const tabMon=document.getElementById('tabMon'),tabSet=document.getElementById('tabSet');
const paneMon=document.getElementById('paneMon'),paneSet=document.getElementById('paneSet');
function show(p){paneMon.classList.toggle('active',p==='mon');paneSet.classList.toggle('active',p==='set');tabMon.classList.toggle('active',p==='mon');tabSet.classList.toggle('active',p==='set');}
tabMon.onclick=()=>show('mon'); tabSet.onclick=()=>show('set');
let selectedCell = 0;
function selectCell(c){selectedCell = c; updateGraph();}
const ctx = graph.getContext('2d');
const chart = new Chart(ctx, {type:'line',data:{labels:[],datasets:[{label:'Célula',data:[],borderWidth:2,borderColor:'#2196F3',backgroundColor:'rgba(33, 150, 243, 0.2)',pointRadius:2}]},options:{animation:false,scales:{x:{display:false},y:{min:3,max:4.3}}}});
const hist=[[],[],[],[],[]]; const labels=[]; const cells=4;
let ws=new WebSocket('ws://'+location.hostname+'/ws');
ws.onmessage=e=>{const d=JSON.parse(e.data); const now = new Date().toLocaleTimeString(); labels.push(now); if(labels.length>60) labels.shift(); d.v.forEach((mv,i)=>{const v=mv/1000; document.getElementById('v'+i).textContent = v.toFixed(3); updateBattery('batt'+i, v, false); hist[i].push(v); if(hist[i].length>60) hist[i].shift();}); const total=d.tot/1000; document.getElementById('tot').textContent=total.toFixed(3); updateBattery('battTot',total,true); hist[4].push(total); if(hist[4].length>60) hist[4].shift(); updateGraph();};
function updateBattery(id,v,isTotal=false){const min=isTotal?3*cells:3,max=isTotal?4.2*cells:4.2;let p=Math.max(0,Math.min(1,(v-min)/(max-min)));const b=document.getElementById(id);b.style.setProperty('--level',(p*100)+'%');let c;const vpc=isTotal?v/cells:v;if(vpc>=3.7)c='#4caf50';else if(vpc>=3.5)c='#ffc107';else c='#f44336';b.style.setProperty('--batt-color',c);}
function updateGraph(){chart.data.labels=[...labels];const t=selectedCell===4,n=t?'Total':'C'+(selectedCell+1);chart.data.datasets[0].label=n+' (V)';chart.data.datasets[0].data=[...hist[selectedCell]];chart.options.scales.y=t?{min:3*cells,max:4.2*cells}:{min:3,max:4.3};chart.update();}
function calib(){const v=[0,1,2,3].map(i=>parseFloat(document.getElementById('in'+i).value)*1000||0);const p=JSON.stringify({v});fetch('/api/calibrate',{method:'POST',headers:{'Content-Type':'application/json'},body:p}).then(r=>r.text()).then(t=>alert("Resposta: "+t)).catch(e=>alert("Erro: "+e));}
function clearLog(){fetch('/api/clear_logs',{method:'POST'});}
</script>
</body>
</html>
)rawliteral";

static void putIndex() {
    File f = SPIFFS.open("/index.html", "w");
    f.print(index_html);
}

void NET_init() {
    SPIFFS.remove("/index.html");
    putIndex();

    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        esp_sleep_enable_timer_wakeup(5 * 60 * 1000000ULL);
        Serial.flush();
        esp_deep_sleep_start();
    }

    configTime(TZ_OFFSET, 0, "pool.ntp.org");
    struct tm tmcheck; time_t now; uint32_t ntpTimeout=millis();
    do{ now=time(nullptr); localtime_r(&now,&tmcheck); }while(now<1600000000 && millis()-ntpTimeout<2000);

    server.on("/", HTTP_GET, [](auto *r){ r->send(SPIFFS, "/index.html", "text/html"); });
    server.on("/download", HTTP_GET, [](auto *r){ r->send(SPIFFS, "/log.csv", "text/csv", true); });
    server.on("/api/raw", HTTP_GET, [](auto *r){ int16_t raw[4]; ADS_raw(raw); StaticJsonDocument<128> d; for(int i=0;i<4;i++) d["raw"][i]=raw[i]; d["lsb"]=0.1875; String o; serializeJson(d,o); r->send(200,"application/json",o); });

    server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            String body((char*)data, len);
            StaticJsonDocument<128> d;
            DeserializationError err = deserializeJson(d, body);
            if (err) {
                Serial.println("[CALIB] Erro ao interpretar JSON");
                request->send(400, "text/plain", "Erro de JSON");
                return;
            }

            Calib novaCalib;
            int16_t raw[4];
            ADS_raw(raw);
            JsonArray v = d["v"];
            bool ok = true;
            for (int i = 0; i < 4; i++) {
                float vReal = v[i];
                if (vReal < 3000 || vReal > 20000 || raw[i] <= 0 || raw[i] > 32000) {
                    Serial.printf("[CALIB] Valor inválido em C%d: v=%.2f, raw=%d\n", i+1, vReal, raw[i]);
                    ok = false;
                    break;
                }
                novaCalib.kDiv[i] = vReal / (raw[i] * 0.1875f);
            }
            if (ok) {
                ADS_setKDiv(novaCalib.kDiv);
                CFG_save(novaCalib);
                Serial.println("[CALIB] Calibração aplicada com sucesso:");
                for (int i = 0; i < 4; i++) Serial.printf("  kDiv[%d] = %.6f\n", i, novaCalib.kDiv[i]);
                request->send(200, "text/plain", "Calibração aplicada");
            } else {
                Serial.println("[CALIB] Calibração abortada – valores inválidos");
                request->send(422, "text/plain", "Valores inválidos");
            }
        });

    server.on("/api/clear_logs", HTTP_POST, [](auto *r){ FS_clearLogs(); r->send(200, "text/plain", "CLEARED"); });
    ws.onEvent([](AsyncWebSocket *srv, AsyncWebSocketClient *cli, AwsEventType type, void *arg, uint8_t *data, size_t len){ if(type==WS_EVT_CONNECT) Serial.println("[WS] cliente conectado"); });
    server.addHandler(&ws);
    server.begin();

    Serial.printf("[NET] Server pronto – acesse: http://%s\n", WiFi.localIP().toString().c_str());
}

void NET_tick(const CellSample &s) {
    StaticJsonDocument<192> d;
    char tbuf[9];
    snprintf(tbuf,9,"%02u:%02u:%02u",(s.epochMs/3600000)%24,(s.epochMs/60000)%60,(s.epochMs/1000)%60);
    d["t"] = tbuf;
    for (uint8_t i = 0; i < 4; i++) d["v"][i] = s.mv[i];
    d["tot"] = s.total;
    String o;
    serializeJson(d, o);
    ws.textAll(o);
}
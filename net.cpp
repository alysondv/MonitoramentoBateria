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

const char *SSID = "Mocoto";
const char *PASS = "gleja23#";
static constexpr long TZ_OFFSET = -3 * 3600;

/* ----------------------------------------------------------
   Single‑page HTML: Monitor + Setup
   ---------------------------------------------------------- */
static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LiPo Monitor</title>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<style>
 body{font-family:Arial,Helvetica,sans-serif;margin:0 auto;max-width:1000px;padding:16px}
 .tabs button{margin-right:8px;padding:8px 16px;border:none;background:#2196F3;color:#fff;border-radius:4px;cursor:pointer}
 .tabs button.active{background:#0d47a1}
 .pane{display:none}
 .pane.active{display:block}
 .cards{display:flex;gap:12px;flex-wrap:wrap}
 .card{border:1px solid #ccc;padding:8px;border-radius:6px;width:120px;text-align:center}
 canvas{max-width:100%;height:140px}
</style>
</head>
<body>
<h2>Monitor de Baterias LiPo</h2>
<div class='tabs'>
 <button id='tabMon' class='active'>Monitor</button>
 <button id='tabSet'>Setup</button>
</div>

<!-- MONITOR PANE -->
<div id='paneMon' class='pane active'>
  <div class='cards'>
    <div class='card'>C1<br><span id='v0'>-</span></div>
    <div class='card'>C2<br><span id='v1'>-</span></div>
    <div class='card'>C3<br><span id='v2'>-</span></div>
    <div class='card'>C4<br><span id='v3'>-</span></div>
  </div>
  <canvas id='g0'></canvas>
  <canvas id='g1'></canvas>
  <canvas id='g2'></canvas>
  <canvas id='g3'></canvas>
  <p>Total: <span id='tot'>-</span></p>
  <a href='/download'>Baixar CSV</a>
</div>

<!-- SETUP PANE -->
<div id='paneSet' class='pane'>
  <h3>Calibração kDiv</h3>
  <p>Digite tensões medidas (V):</p>
  C1 <input id='in0' size='4'> V<br>
  C1+C2 <input id='in1' size='4'> V<br>
  C1+C2+C3 <input id='in2' size='4'> V<br>
  Pack <input id='in3' size='4'> V<br>
  <button onclick='calib()'>Calibrar</button>
  <h3>Logs</h3>
  <button onclick='clearLog()'>Limpar logs</button>
</div>

<script>
const tabMon=document.getElementById('tabMon'),tabSet=document.getElementById('tabSet');
const paneMon=document.getElementById('paneMon'),paneSet=document.getElementById('paneSet');
function show(p){paneMon.classList.toggle('active',p==='mon');paneSet.classList.toggle('active',p==='set');tabMon.classList.toggle('active',p==='mon');tabSet.classList.toggle('active',p==='set');}
 tabMon.onclick=()=>show('mon');
 tabSet.onclick=()=>show('set');

const ctx=[g0,g1,g2,g3].map(c=>c.getContext('2d'));
const charts=ctx.map((c,i)=>new Chart(c,{type:'line',data:{labels:[],datasets:[{label:'C'+(i+1)+' (V)',data:[],borderWidth:1}]},options:{animation:false,scales:{x:{display:false},y:{min:3,max:4.3}}}}));

let ws=new WebSocket('ws://'+location.hostname+'/ws');
ws.onmessage=e=>{const d=JSON.parse(e.data);d.v.forEach((mv,i)=>{document.getElementById('v'+i).textContent=(mv/1000).toFixed(3);charts[i].data.labels.push(d.t);charts[i].data.datasets[0].data.push(mv/1000);if(charts[i].data.labels.length>60){charts[i].data.labels.shift();charts[i].data.datasets[0].data.shift();}charts[i].update();});document.getElementById('tot').textContent=(d.tot/1000).toFixed(3)+' V';};

function calib(){const v=[0,1,2,3].map(i=>parseFloat(document.getElementById('in'+i).value)*1000||0);fetch('/api/calibrate',{method:'POST',body:JSON.stringify({v})});}
function clearLog(){fetch('/api/clear_logs',{method:'POST'});} 
</script>
</body>
</html>
)rawliteral";

static void putIndex(){ File f=SPIFFS.open("/index.html","w"); f.print(index_html);} 

void NET_init(){
    if(!SPIFFS.exists("/index.html")) putIndex(); else { SPIFFS.remove("/index.html"); putIndex(); }

    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASS);
    uint32_t t0 = millis();
    bool wifiOk = false;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 3000) delay(100); // Aguarda só 3 segundos
    if (WiFi.status() == WL_CONNECTED) {
        wifiOk = true;
        Serial.printf("[NET] Conectado ao WiFi: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[NET] WiFi não conectado, ativando modo AP...");
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP("BatteryMonitor_Config", "12345678");
        Serial.printf("[NET] AP ativo – acesse: http://%s\n", WiFi.softAPIP().toString().c_str());
    }

    configTime(TZ_OFFSET,0,"pool.ntp.org");
    struct tm tmcheck; time_t now; uint32_t ntpTimeout=millis();
    do{ now=time(nullptr); localtime_r(&now,&tmcheck); }while(now<1600000000 && millis()-ntpTimeout<2000);

    server.on("/",HTTP_GET,[](auto *r){ r->send(SPIFFS,"/index.html","text/html");});
    server.on("/download",HTTP_GET,[](auto *r){ r->send(SPIFFS,"/log.csv","text/csv",true);} );
    server.on("/api/raw",HTTP_GET,[](auto *r){ int16_t raw[4]; ADS_raw(raw); StaticJsonDocument<128> d; for(int i=0;i<4;i++) d["raw"][i]=raw[i]; d["lsb"]=0.1875; String o; serializeJson(d,o); r->send(200,"application/json",o);} );
    server.on("/api/calibrate",HTTP_POST,[](auto *r){ Calib c; int16_t raw[4]; ADS_raw(raw); StaticJsonDocument<128> d; deserializeJson(d,r->arg("plain")); for(int i=0;i<4;i++){ float v=d["v"][i]; if(v>500) c.kDiv[i]=v/(raw[i]*0.1875);} CFG_save(c); ADS_setKDiv(c.kDiv); r->send(200,"text/plain","OK"); });
    server.on("/api/clear_logs",HTTP_POST,[](auto *r){ FS_clearLogs(); r->send(200,"text/plain","CLEARED"); } );
    ws.onEvent([](AsyncWebSocket *srv,AsyncWebSocketClient *cli,AwsEventType type,void *arg,uint8_t *data,size_t len){ if(type==WS_EVT_CONNECT) Serial.println("[WS] cliente conectado"); });
    server.addHandler(&ws);
    server.begin();

    Serial.printf("[NET] Server pronto – acesse: http://%s\n", WiFi.localIP().toString().c_str());
}

void NET_tick(const CellSample &s){
    StaticJsonDocument<192> d; char tbuf[9];
    snprintf(tbuf,9,"%02u:%02u:%02u",(s.epochMs/3600000)%24,(s.epochMs/60000)%60,(s.epochMs/1000)%60);
    d["t"]=tbuf; for(uint8_t i=0;i<4;i++) d["v"][i]=s.mv[i]; d["tot"]=s.total; String o; serializeJson(d,o); ws.textAll(o);
}
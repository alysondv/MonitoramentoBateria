#include "net.h"
#include <WiFi.h>
#include <time.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// SPA – HTML embutido
static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Monitor LiPo</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body{font-family:Arial,Helvetica,sans-serif;max-width:1000px;margin:0 auto;padding:10px;}
    h1{text-align:center;margin:0 0 10px}
    .cells{display:flex;flex-wrap:wrap;gap:20px;justify-content:center}
    .card{border:1px solid #ccc;border-radius:8px;padding:10px;width:220px;box-shadow:0 2px 4px rgba(0,0,0,.1);}
    .voltage{font-size:1.4em;font-weight:bold;text-align:center;margin:4px 0;}
    .lbl{font-weight:bold;text-align:center}
    canvas{width:200px!important;height:120px!important}
    #status{padding:8px;border-radius:4px;margin:8px 0;text-align:center;background:#e8f5e9;color:#2e7d32}
    .warn{background:#fff8e1;color:#ff8f00}
    .alarm{background:#ffebee;color:#c62828}
    .btn{display:block;width:220px;margin:15px auto;padding:10px;text-align:center;background:#2196F3;color:#fff;text-decoration:none;border-radius:5px}
  </style>
</head>
<body>
<h1>Monitor de Baterias LiPo</h1>
<div id="status">Conectando…</div>
<div class="cells">
  <div class="card"><div class="lbl">Célula 1</div><div id="v0" class="voltage">--</div><canvas id="c0"></canvas></div>
  <div class="card"><div class="lbl">Célula 2</div><div id="v1" class="voltage">--</div><canvas id="c1"></canvas></div>
  <div class="card"><div class="lbl">Célula 3</div><div id="v2" class="voltage">--</div><canvas id="c2"></canvas></div>
  <div class="card"><div class="lbl">Célula 4</div><div id="v3" class="voltage">--</div><canvas id="c3"></canvas></div>
</div>
<a class="btn" href="/download">Baixar CSV</a>
<script>
const ws=new WebSocket('ws://'+location.host+'/ws');
const st=document.getElementById('status');
const vLbl=[document.getElementById('v0'),document.getElementById('v1'),document.getElementById('v2'),document.getElementById('v3')];
const charts=[];
for(let i=0;i<4;i++){
  const ctx=document.getElementById('c'+i).getContext('2d');
  charts[i]=new Chart(ctx,{type:'line',data:{labels:[],datasets:[{data:[],fill:false,borderColor:'#4CAF50',pointRadius:0}]},options:{animation:false,scales:{x:{display:false},y:{display:false,min:3000,max:4300}}}});
}
function pushPoint(i,volt,label){
  const d=charts[i].data;
  d.labels.push(label);
  d.datasets[0].data.push(volt);
  if(d.labels.length>60){d.labels.shift();d.datasets[0].data.shift();}
  charts[i].update('none');
}
ws.onopen=_=>{st.textContent='Conectado';st.className='';};
ws.onclose=_=>{st.textContent='Desconectado';st.className='warn';};
ws.onmessage=e=>{
  const d=JSON.parse(e.data);
  d.v.forEach((mv,i)=>{
    vLbl[i].textContent=(mv/1000).toFixed(2)+' V';
    pushPoint(i,mv,d.t);
  });
};
</script>
</body>
</html>
)rawliteral";

// --- Wi‑Fi credenciais (edite) ---
static const char *ssid="Mocoto";
static const char *pass="gleja23#";

bool NET_init(){
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid,pass);
    uint32_t t0=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-t0<5000) delay(100);
    Serial.printf("IP: %s\n",WiFi.localIP().toString().c_str());

    configTime(-3*3600,0,"pool.ntp.org");

    // SPIFFS page
    if(!SPIFFS.exists("/index.html")){
        File f=SPIFFS.open("/index.html",FILE_WRITE);
        f.print(index_html); f.close();
    }
    server.on("/",HTTP_GET,[](auto *req){req->send(SPIFFS,"/index.html","text/html");});
    server.on("/download",HTTP_GET,[](auto *req){req->send(SPIFFS,"/log.csv","text/csv",true);});
    ws.onEvent([](auto *s,auto *c,auto type,void *p,uint8_t* d,size_t l){});
    server.addHandler(&ws);
    server.begin();
    return true;
}

void NET_tick(const CellSample &s){
    static char buf[128];
    sprintf(buf,"{\"t\":\"%s\",\"v\":[%u,%u,%u,%u],\"tot\":%u}",
            "--",s.mv[0],s.mv[1],s.mv[2],s.mv[3],s.total);
    ws.textAll(buf);
}

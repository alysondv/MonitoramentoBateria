#include "net.h"
#include <SPIFFS.h>
#include <time.h>

static AsyncWebServer srv(80);
static AsyncWebSocket ws("/ws");

static String genHTML(){
    return R"html(
<!DOCTYPE html><html><head><meta charset='utf-8'><title>Battery Monitor</title>
<style>body{font-family:Arial;text-align:center}table{margin:auto;border-collapse:collapse}td,th{padding:6px 12px;border:1px solid #ccc}</style>
</head><body><h2>Battery Monitor</h2>
<table id="t"><tr><th>Time</th><th>C1 (mV)</th><th>C2</th><th>C3</th><th>C4</th><th>Total</th></tr></table>
<script>
const t=document.getElementById('t');
const w=new WebSocket('ws://'+location.host+'/ws');
w.onmessage=e=>{const d=JSON.parse(e.data);
  const r=t.insertRow(1);
  r.insertCell().innerText=d.t;
  for(const v of d.v) r.insertCell().innerText=v;
  r.insertCell().innerText=d.tot;
  if(t.rows.length>20) t.deleteRow(-1);
};</script></body></html>)html";
}

bool NET_init(const char* ssid,const char* pwd){
    WiFi.mode(WIFI_STA); WiFi.begin(ssid,pwd);
    uint32_t t0=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-t0<5000) delay(200);
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    configTime(-3*3600,0,"pool.ntp.org");
    if(!SPIFFS.exists("/index.html")){
        File f=SPIFFS.open("/index.html", FILE_WRITE);
        f.print(genHTML()); f.close();
    }
    srv.on("/",HTTP_GET,[](auto*r){r->send(SPIFFS,"/index.html","text/html");});
    srv.on("/download",HTTP_GET,[](auto*r){r->send(SPIFFS,"/log.csv","text/csv",true);});
    srv.addHandler(&ws); srv.begin();
    return true;
}

void NET_tick(const CellSample &s){
    char buf[128];
    snprintf(buf,sizeof(buf),"{\"t\":\"%02u:%02u:%02u\",\"v\":[%u,%u,%u,%u],\"tot\":%u}",
             (s.epochMs/3600000)%24,(s.epochMs/60000)%60,(s.epochMs/1000)%60,
             s.mv[0],s.mv[1],s.mv[2],s.mv[3],s.total);
    ws.textAll(buf);
}

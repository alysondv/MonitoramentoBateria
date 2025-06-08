/*
 * BatteryMonitor – Web + WS v4.1 (startup quick‑fix)
 * =================================================
 * – Desativa o task‑watchdog (“task_wdt”) que poluía o Serial.
 * – Limita a tentativa de Wi‑Fi a 5 s; se falhar, segue offline.
 * =============================================
 * • Oversampling (16× @ 475 SPS) -> C1…C4 + Total
 * • Timestamp HH:MM:SS via NTP (UTC‑3, BR)
 * • Servidor HTTP (porta 80) + WebSocket "/ws"
 * • Página única SPA em / (carregada de SPIFFS)
 * • WS envia JSON: {"t":"HH:MM:SS","v":[C1,C2,C3,C4],"tot":Total}
 *
 * 08‑Jun‑2025
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Adafruit_ADS1X15.h>

// ─── Credenciais Wi‑Fi ───
const char* SSID     = "Mocoto";
const char* PASSWORD = "gleja23#";

// ─── NTP ───
const char* NTP_SERVER      = "pool.ntp.org";
constexpr long  UTC_OFFSET  = -3 * 3600;  // Brasil‑São Paulo (UTC‑3)
constexpr long  DST_OFFSET  = 0;

// ─── I²C / ADS1115 ───
constexpr uint8_t I2C_SDA = 42;
constexpr uint8_t I2C_SCL = 41;
Adafruit_ADS1115 ads;

constexpr size_t NUM_CH     = 4;
constexpr uint8_t N_SAMPLE  = 16;       // oversampling
constexpr float  LSB_MV     = 0.1875f;  // PGA ±6,144 V
float kDiv[NUM_CH] = {1.042f, 2.109f, 3.023f, 4.033f};

// ─── Web ───
AsyncWebServer    server(80);
AsyncWebSocket    ws("/ws");

// ─── Tempo ───
String getTimestamp()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

// ─── Func protótipos ───
int16_t readAvg(uint8_t ch);
void   sendWS(float cell[NUM_CH], float total);
bool   initWiFi();
void   initSPIFFS();
void   initWeb();

// ─── Setup ───
void setup()
{
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ads.begin(0x48)) {
    Serial.println("ADS1115 não encontrado");
    while (true) delay(1000);
  }
  ads.setGain(GAIN_TWOTHIRDS);
  ads.setDataRate(RATE_ADS1115_475SPS);

  initSPIFFS();
  initWiFi();
  configTime(UTC_OFFSET, DST_OFFSET, NTP_SERVER);
  initWeb();

  Serial.println("Sistema pronto");
}

// ─── Loop ───
void loop()
{
  static uint32_t nextMs = 0;
  uint32_t now = millis();
  if (now < nextMs) { ws.cleanupClients(); return; }
  nextMs = now + 500; // 2 Hz

  int16_t raw[NUM_CH];
  float   vAbs[NUM_CH];
  for (uint8_t i = 0; i < NUM_CH; ++i) {
    raw[i]  = readAvg(i);
    vAbs[i] = raw[i] * LSB_MV * kDiv[i];
  }
  float cell[NUM_CH];
  cell[0] = vAbs[0];
  cell[1] = vAbs[1] - vAbs[0];
  cell[2] = vAbs[2] - vAbs[1];
  cell[3] = vAbs[3] - vAbs[2];
  float total = vAbs[3];

  // Serial CSV
  Serial.printf("%s,%.0f,%.0f,%.0f,%.0f,%.0f\n", getTimestamp().c_str(),
               cell[0], cell[1], cell[2], cell[3], total);

  sendWS(cell, total);
  ws.cleanupClients();
}

// ─── Funções ───
int16_t readAvg(uint8_t ch)
{
  int32_t acc = 0;
  for (uint8_t i = 0; i < N_SAMPLE; ++i) acc += ads.readADC_SingleEnded(ch);
  return acc / N_SAMPLE;
}

void sendWS(float cell[NUM_CH], float total)
{
  if (ws.count() == 0) return;
  String json = "{\"t\":\"" + getTimestamp() + "\",\"v\":";
  json += "[" + String((int)cell[0]);
  for (uint8_t i = 1; i < NUM_CH; ++i) json += "," + String((int)cell[i]);
  json += "],\"tot\":" + String((int)total) + "}";
  ws.textAll(json);
}

// ─── Wi‑Fi ───
bool initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Conectando‑se ao Wi‑Fi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" falhou");
    return false;
  }
  Serial.print("\nIP: "); Serial.println(WiFi.localIP());
  return true;
}

// ─── SPIFFS & Web ───
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LiPo Monitor</title>
<style>
 body{font-family:sans-serif;text-align:center;margin:0 auto;max-width:480px;padding:1rem}
 #cells{display:grid;grid-template-columns:repeat(2,1fr);gap:1rem;margin-top:1rem}
 .card{border:1px solid #888;border-radius:8px;padding:0.5rem}
 .v{font-size:1.4rem;font-weight:700}
</style></head><body>
<h3>LiPo 4 S Monitor</h3><div id='t'>--:--:--</div>
<div id='cells'></div><script>
let ws;
function connect(){ws=new WebSocket('ws://'+location.host+'/ws');
 ws.onopen=()=>console.log('ws ok');
 ws.onmessage=e=>{const d=JSON.parse(e.data);document.getElementById('t').textContent=d.t;
 const arr=d.v;let html='';arr.forEach((v,i)=>{html+=`<div class='card'>C${i+1}<div class='v'>${(v/1000).toFixed(3)} V</div></div>`});
 html+=`<div class='card' style='grid-column:span 2'>Total<div class='v'>${(d.tot/1000).toFixed(3)} V</div></div>`;
 document.getElementById('cells').innerHTML=html;};
 ws.onclose=()=>setTimeout(connect,2000);}
connect();
</script></body></html>
)rawliteral";

void initSPIFFS()
{
  if (!SPIFFS.begin(true)) { Serial.println("SPIFFS erro"); while(true) delay(1000);}  
  if (!SPIFFS.exists("/index.html")) {
    File f = SPIFFS.open("/index.html", FILE_WRITE);
    if (f) { f.print(INDEX_HTML); f.close(); }
  }
}

void initWeb()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(SPIFFS, "/index.html", "text/html");
  });
  ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t){});
  server.addHandler(&ws);
  server.begin();
}

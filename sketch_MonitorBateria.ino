/*
 * BatteryMonitor – Web + WS + LOG v5.2 (I²C 50 kHz & SPIFFS fix)
 * =============================================================
 * 1. I²C clock reduzido para 50 kHz → estabilidade com cabos longos.
 * 2. Oversampling 8×; inserido delay µs para não atropelar conversões.
 * 3. Opção one‑shot de formatar SPIFFS se detectar -10025 (descomentar).
 * 4. CSV: HH:MM:SS,C1,C2,C3,C4,Total (mV inteiros).
 *
 * Requisitos de hardware: ADS1115 alimentado a 3V3, pull‑ups 3k3 → SDA/SCL.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_ADS1X15.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// ───────── CONFIG Wi‑Fi / NTP ─────────
const char *ssid = "Mocoto";
const char *password = "gleja23#";
#define UTC_OFFSET_SEC   (-3 * 3600)
#define DST_OFFSET_SEC    0

// ───────── I²C ─────────
constexpr uint8_t I2C_SDA = 42;
constexpr uint8_t I2C_SCL = 41;
constexpr uint32_t I2C_HZ  = 50000;    // 50 kHz

// ───────── ADS1115 ─────────
Adafruit_ADS1115 ads;
constexpr uint8_t N_OVERSAMPLE = 8;    // 8 leituras → √8 ≈ 2,8× <ruído>
constexpr float LSB_MV = 0.1875f;      // PGA ±6,144 V

// kDiv obtidos pelo usuário
float kDiv[4] = {1.042f, 2.109f, 3.023f, 4.033f};

// ───────── Web ─────────
AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");

// ───────── Funções auxiliares ─────────
String getTimestamp() {
  struct tm t;
  if (!getLocalTime(&t, 2000)) return "00:00:00";
  char buf[9];
  strftime(buf, 9, "%H:%M:%S", &t);
  return String(buf);
}

bool safeRead(uint8_t ch, int16_t &v) {
  for (uint8_t a = 0; a < 3; ++a) {
    v = ads.readADC_SingleEnded(ch);
    if (v != 0xFFFF) return true;
    delay(2);
  }
  return false;
}

int16_t readAvg(uint8_t ch) {
  int32_t acc = 0;
  int16_t val;
  for (uint8_t i = 0; i < N_OVERSAMPLE; ++i) {
    if (!safeRead(ch, val)) return -1;
    acc += val;
    delayMicroseconds(1000 / N_OVERSAMPLE); // ~125 µs → 8*125 = 1 ms (≥2,1 ms total)
  }
  return acc / N_OVERSAMPLE;
}

void appendLog(const String &line) {
  static File f = SPIFFS.open("/log.csv", FILE_APPEND);
  if (!f) return;
  if (f.size() > 512000) {
    f.close();
    if (SPIFFS.exists("/log_old.csv")) SPIFFS.remove("/log_old.csv");
    SPIFFS.rename("/log.csv", "/log_old.csv");
    f = SPIFFS.open("/log.csv", FILE_WRITE);
    f.println("Time,C1,C2,C3,C4,Total");
  }
  f.println(line);
  f.flush();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL, I2C_HZ);

  // ADS init
  if (!ads.begin(0x48)) {
    Serial.println("[ADS] Falha inicialização");
    while (true) delay(1000);
  }
  ads.setGain(GAIN_TWOTHIRDS);
  ads.setDataRate(RATE_ADS1115_475SPS);

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] mount falhou – format...");
    // SPIFFS.format(); // <- descomente uma vez se -10025 persistir
    if (!SPIFFS.begin(true)) Serial.println("[SPIFFS] ERRO crítico");
  }
  if (!SPIFFS.exists("/log.csv")) {
    File f = SPIFFS.open("/log.csv", FILE_WRITE);
    f.println("Time,C1,C2,C3,C4,Total");
    f.close();
  }

  // Wi‑Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 5000) {
    delay(200);
    Serial.print('.');
  }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());
  configTime(UTC_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org");

  // Página SPA mínima se faltar
  if (!SPIFFS.exists("/index.html")) {
    File f = SPIFFS.open("/index.html", FILE_WRITE);
    f.print("<html><body><h2>Battery Monitor</h2><button onclick=location.href='/download'>Baixar CSV</button><pre id='d'></pre><script>let w=new WebSocket('ws://" + WiFi.localIP().toString() + "/ws');w.onmessage=e=>{document.getElementById('d').textContent=e.data}</script></body></html>");
    f.close();
  }

  server.on("/", HTTP_GET, [](auto *r){ r->send(SPIFFS, "/index.html", "text/html"); });
  server.on("/download", HTTP_GET, [](auto *r){ r->send(SPIFFS, "/log.csv", "text/csv", true); });
  ws.onEvent([](auto*,auto* c,auto t,void*,uint8_t*,size_t){ if(t==WS_EVT_CONNECT) Serial.printf("[WS] cli %u\n",c->id()); });
  server.addHandler(&ws);
  server.begin();
}

void loop() {
  static uint32_t last = 0;  // 500 ms
  if (millis() - last < 500) return;
  last = millis();

  int16_t raw[4];
  for (uint8_t i = 0; i < 4; ++i) raw[i] = readAvg(i);

  float vAbs[4];
  for (uint8_t i = 0; i < 4; ++i) vAbs[i] = raw[i] * LSB_MV * kDiv[i];

  float cell[4] = {vAbs[0], vAbs[1]-vAbs[0], vAbs[2]-vAbs[1], vAbs[3]-vAbs[2]};
  float total   = vAbs[3];

  String stamp = getTimestamp();
  String csv = stamp;
  for (uint8_t i=0;i<4;++i) csv += "," + String((int)cell[i]);
  csv += "," + String((int)total);

  appendLog(csv);
  ws.textAll(csv);
  Serial.println(csv);
}

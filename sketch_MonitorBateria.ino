/*
 * BatteryMonitorMinimal – OVERSAMPLING + CÉLULAS + NTP v3
 * =======================================================
 * – Oversampling de 16 leituras @ 475 SPS.
 * – Converte acumulados em tensões individuais (C1…C4).
 * – Timestamp em tempo‑real HH:MM:SS via NTP.
 * – CSV limpo: HH:MM:SS,C1_mV,C2_mV,C3_mV,C4_mV,Total_mV
 *
 * 08‑Jun‑2025.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_ADS1X15.h>
#include <time.h>

// ─── Hardware / I²C ───
constexpr uint8_t I2C_SDA = 42;
constexpr uint8_t I2C_SCL = 41;
constexpr uint8_t ADS_ADDR = 0x48;

// ─── Oversampling ───
constexpr uint8_t  N_OVERSAMPLE    = 16;
constexpr uint16_t LOOP_INTERVAL_MS = 500;  // taxa de log ~2 Hz

// ─── ADS1115 constantes ───
constexpr size_t NUM_CH = 4;
constexpr float  LSB_MV  = 0.1875f;          // mV/bit @ PGA ±6,144 V

// ─── Divisores calibrados (08‑Jun‑2025) ───
float kDiv[NUM_CH] = {
  1.042f,  // A0 – C1
  2.109f,  // A1 – C1+C2
  3.023f,  // A2 – C1+C2+C3
  4.033f   // A3 – Pack
};

// ─── Wi‑Fi + NTP ───
const char* WIFI_SSID = "Mocoto";
const char* WIFI_PASS = "gleja23#";
const char* NTP_SERVER = "pool.ntp.org";
constexpr long  GMT_OFFSET  = -3 * 3600; // Brasil (‑03:00)
constexpr int   DST_OFFSET  = 0;

// ─── Globais ───
Adafruit_ADS1115 ads;
uint32_t nextLoop = 0;

// ─────────────────────────────────────────────────────────────
// Funções auxiliares
// ─────────────────────────────────────────────────────────────

int16_t readAveraged(uint8_t ch)
{
  int32_t acc = 0;
  for (uint8_t i = 0; i < N_OVERSAMPLE; ++i)
    acc += ads.readADC_SingleEnded(ch);
  return acc / N_OVERSAMPLE;
}

String getTimestamp()
{
  time_t now = time(nullptr);
  if (now < 8 * 3600) {          // tempo não sincronizado
    // fallback para millis /1000
    uint32_t s = millis() / 1000;
    char buf[9];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (s/3600)%24, (s/60)%60, s%60);
    return String(buf);
  }
  struct tm tminfo;
  localtime_r(&now, &tminfo);
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &tminfo);
  return String(buf);
}

void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
  }
}

void setupNTP()
{
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
}

// ─── Setup ───
void setup()
{
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ads.begin(ADS_ADDR)) {
    Serial.println(F("[ADS] Falha ao inicializar"));
    while (true) delay(1000);
  }
  ads.setGain(GAIN_TWOTHIRDS);              // ±6,144 V
  ads.setDataRate(RATE_ADS1115_475SPS);

  connectWiFi();     // tenta Wi‑Fi (15 s máx.)
  setupNTP();        // dispara sincronização

  Serial.println(F("HH:MM:SS,C1_mV,C2_mV,C3_mV,C4_mV,Total_mV"));
}

// ─── Loop ───
void loop()
{
  const uint32_t nowMs = millis();
  if (nowMs < nextLoop) return;
  nextLoop = nowMs + LOOP_INTERVAL_MS;

  // 1. Amostras e conversão para acumulados
  int16_t raw[NUM_CH];
  float   vAbs[NUM_CH];
  for (uint8_t i = 0; i < NUM_CH; ++i) {
    raw[i]  = readAveraged(i);
    vAbs[i] = raw[i] * LSB_MV * kDiv[i];
  }

  // 2. Tensões individuais
  float cell[NUM_CH];
  cell[0] = vAbs[0];
  cell[1] = vAbs[1] - vAbs[0];
  cell[2] = vAbs[2] - vAbs[1];
  cell[3] = vAbs[3] - vAbs[2];
  float pack = vAbs[3];

  // 3. Timestamp
  String stamp = getTimestamp();

  // 4. CSV
  Serial.printf("%s,%.0f,%.0f,%.0f,%.0f,%.0f\n",
               stamp.c_str(), cell[0], cell[1], cell[2], cell[3], pack);
}

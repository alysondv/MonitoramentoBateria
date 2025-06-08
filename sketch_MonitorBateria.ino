/*
 * BatteryMonitorMinimal – RAW + kDiv
 * =========================================
 * Objetivo desta versão: depurar divisores.
 *  – Lê os 4 canais single‑ended do ADS1115.
 *  – Imprime: ms,raw0,raw1,raw2,raw3,mV0,mV1,mV2,mV3
 *    onde mVx = raw × 0,1875 mV/bit × kDiv[x].
 *  – Não aplica kGain nem offsets, nem calcula tensões diferenciais.
 *
 * Configurado para ADS1115 em PGA ±6,144 V (0,1875 mV/bit).
 * Serial: 115 200 baud.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ─── Hardware / I²C ──────────────────────────────────────────
constexpr uint8_t I2C_SDA = 42;
constexpr uint8_t I2C_SCL = 41;
constexpr uint8_t ADS_ADDR = 0x48;

// ─── Temporização ───────────────────────────────────────────
constexpr uint16_t SAMPLE_INTERVAL_MS = 500; // 2 Hz

// ─── ADS1115 Constantes ────────────────────────────────────
constexpr size_t NUM_CH = 4;
constexpr float  LSB_MV   = 0.1875f;          // mV/bit @ PGA ±6,144 V

// ─── Divisores resistivos (valores MEDIDOS) ────────────────
float kDiv[NUM_CH] = {
  1.042f,  // A0 – Célula 1 (efetivo)
  2.109f,  // A1 – C1+C2
  3.023f,  // A2 – C1+C2+C3
  4.033f   // A3 – Pack total
};

// ─── Globais ───────────────────────────────────────────────
Adafruit_ADS1115 ads;
uint32_t nextSample = 0;
uint32_t lineCnt    = 0;

// ─── Setup ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ads.begin(ADS_ADDR)) {
    Serial.println(F("[ADS] Falha ao inicializar – verifique conexões!"));
    while (true) delay(1000);
  }
  ads.setGain(GAIN_TWOTHIRDS); // ±6,144 V

  Serial.println(F("ms,raw0,raw1,raw2,raw3,mV0,mV1,mV2,mV3"));
}

// ─── Loop ──────────────────────────────────────────────────
void loop() {
  const uint32_t now = millis();
  if (now < nextSample) return;
  nextSample = now + SAMPLE_INTERVAL_MS;

  // 1. Leitura bruta
  int16_t raw[NUM_CH];
  for (uint8_t i = 0; i < NUM_CH; ++i) {
    raw[i] = ads.readADC_SingleEnded(i);
  }

  // 2. Conversão com kDiv
  float mv[NUM_CH];
  for (uint8_t i = 0; i < NUM_CH; ++i) {
    mv[i] = raw[i] * LSB_MV * kDiv[i];
  }

  // 3. Cabeçalho periódico para log longo
  if (++lineCnt % 50 == 1) {
    Serial.println(F("ms,raw0,raw1,raw2,raw3,mV0,mV1,mV2,mV3"));
  }

  // 4. Impressão CSV
  Serial.printf("%lu,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.1f\n",
               now,
               raw[0], raw[1], raw[2], raw[3],
               mv[0], mv[1], mv[2], mv[3]);
}

/*
 * BatteryMonitorMinimal – OVERSAMPLING + CÉLULAS v2.1
 * ====================================================
 * – Mantém oversampling de 16 leituras @ 475 SPS
 * – Calcula tensões individuais (C1…C4) e total do pack
 * – CSV enxuto: ms,C1_mV,C2_mV,C3_mV,C4_mV,Total_mV (inteiros)
 *
 * 08‑Jun‑2025.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ─── Hardware / I²C ───
constexpr uint8_t I2C_SDA = 42;
constexpr uint8_t I2C_SCL = 41;
constexpr uint8_t ADS_ADDR = 0x48;

// ─── Temporização ───
constexpr uint16_t SAMPLE_PERIOD_MS = 500; // logger a 2 Hz
constexpr uint8_t  N_OVERSAMPLE     = 16;  // 16 médias → ~34 ms/ch

// ─── ADS1115 ───
constexpr size_t NUM_CH = 4;
constexpr float  LSB_MV  = 0.1875f;        // mV/bit @ PGA ±6,144 V

// ─── Divisores efetivos ───
float kDiv[NUM_CH] = {
  1.042f,  // A0 – C1 acumulado
  2.109f,  // A1 – C1+C2
  3.023f,  // A2 – C1+C2+C3
  4.033f   // A3 – Pack total
};

Adafruit_ADS1115 ads;
uint32_t nextSample = 0;
uint32_t lineCnt    = 0;

// Oversampling helper
int16_t readAveraged(uint8_t ch) {
  int32_t acc = 0;
  for (uint8_t i = 0; i < N_OVERSAMPLE; ++i) {
    acc += ads.readADC_SingleEnded(ch);
  }
  return acc / N_OVERSAMPLE;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ads.begin(ADS_ADDR)) {
    Serial.println(F("[ADS] Falha ao inicializar – confira fiação!"));
    while (true) delay(1000);
  }

  ads.setGain(GAIN_TWOTHIRDS);          // ±6,144 V
  ads.setDataRate(RATE_ADS1115_475SPS); // melhor SNR/time

  Serial.println(F("ms,C1_mV,C2_mV,C3_mV,C4_mV,Total_mV"));
}

void loop() {
  const uint32_t now = millis();
  if (now < nextSample) return;
  nextSample = now + SAMPLE_PERIOD_MS;

  int16_t raw[NUM_CH];
  float   mv [NUM_CH];
  float   cell[NUM_CH];

  // 1. Amostragem e conversão acumulada
  for (uint8_t i = 0; i < NUM_CH; ++i) {
    raw[i] = readAveraged(i);
    mv[i]  = raw[i] * LSB_MV * kDiv[i];
  }

  // 2. Diferenciação → tensão individual
  cell[0] = mv[0];
  cell[1] = mv[1] - mv[0];
  cell[2] = mv[2] - mv[1];
  cell[3] = mv[3] - mv[2];
  float pack = mv[3];

  // 3. Cabeçalho periódico
  if (++lineCnt % 50 == 1) {
    Serial.println(F("ms,C1_mV,C2_mV,C3_mV,C4_mV,Total_MV"));
  }

  // 4. CSV – inteiros mV (%.0f)
  Serial.printf("%lu,%.0f,%.0f,%.0f,%.0f,%.0f\n",
                now, cell[0], cell[1], cell[2], cell[3], pack);
}

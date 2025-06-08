/*
 * BatteryMonitorMinimal – OVERSAMPLING v1
 * ======================================
 * Lê os quatro canais single‑ended do ADS1115, aplica
 * oversampling de N leituras e converte em mV via kDiv.
 * Imprime CSV:
 *   ms,raw0,raw1,raw2,raw3,mV0,mV1,mV2,mV3
 *
 * Mudanças desta versão:
 *   • PGA continua ±6,144 V (GAIN_TWOTHIRDS) → 0,1875 mV/bit.
 *   • Data‑rate fixada em 475 SPS (melhor relação ruído/tempo).
 *   • Oversampling (média) de 16 amostras por canal → desvio‑padrão ≈¼.
 *   • kDiv calibrados empiricamente (08‑Jun‑2025).
 *
 * 08‑Jun‑2025.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ─── Hardware / I2C ───
constexpr uint8_t I2C_SDA = 42;
constexpr uint8_t I2C_SCL = 41;
constexpr uint8_t ADS_ADDR = 0x48;

// ─── Temporização ───
constexpr uint16_t SAMPLE_PERIOD_MS = 500; // logger a 2 Hz
constexpr uint8_t  N_OVERSAMPLE     = 16;  // 16 médias → 34 ms/ch aprox.

// ─── ADS1115 constantes ───
constexpr size_t NUM_CH = 4;
constexpr float  LSB_MV  = 0.1875f;        // mV/bit @ PGA ±6,144 V

// ─── Divisores efetivos ───
float kDiv[NUM_CH] = {
  1.042f,  // A0 – C1
  2.109f,  // A1 – C1+C2
  3.023f,  // A2 – C1+C2+C3
  4.033f   // A3 – Pack
};

// ─── Globais ───
Adafruit_ADS1115 ads;
uint32_t nextSample = 0;
uint32_t lineCnt    = 0;

// ─── Prototipagem ───
int16_t readAveraged(uint8_t ch);

// ─── Setup ───
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ads.begin(ADS_ADDR)) {
    Serial.println(F("[ADS] Falha ao inicializar – confira fiação!"));
    while (true) delay(1000);
  }

  ads.setGain(GAIN_TWOTHIRDS);               // ±6,144 V → 0,1875 mV/bit
  ads.setDataRate(RATE_ADS1115_475SPS);      // taxa com melhor SNR/time

  Serial.println(F("ms,raw0,raw1,raw2,raw3,mV0,mV1,mV2,mV3"));
}

// ─── Função de oversampling ───
int16_t readAveraged(uint8_t ch)
{
  int32_t acc = 0;
  for (uint8_t i = 0; i < N_OVERSAMPLE; ++i) {
    acc += ads.readADC_SingleEnded(ch);
  }
  return static_cast<int16_t>(acc / N_OVERSAMPLE);
}

// ─── Loop ───
void loop() {
  const uint32_t now = millis();
  if (now < nextSample) return;
  nextSample = now + SAMPLE_PERIOD_MS;

  int16_t raw[NUM_CH];
  float   mv [NUM_CH];

  // Amostragem + conversão
  for (uint8_t i = 0; i < NUM_CH; ++i) {
    raw[i] = readAveraged(i);
    mv[i]  = raw[i] * LSB_MV * kDiv[i];
  }

  // Cabeçalho periódico
  if (++lineCnt % 50 == 1) {
    Serial.println(F("ms,raw0,raw1,raw2,raw3,mV0,mV1,mV2,mV3"));
  }

  Serial.printf("%lu,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.1f\n",
               now, raw[0], raw[1], raw[2], raw[3],
               mv[0], mv[1], mv[2], mv[3]);
}

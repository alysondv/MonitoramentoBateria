#include "ads_driver.h"
#include <Wire.h>
#include <math.h>
#include <string.h>

static Adafruit_ADS1115 ads;
static constexpr uint8_t N = 8;           // Oversampling
static constexpr float LSB = 0.1875f;     // mV/bit @ ±6.144V
static float kDiv[4] = {1.042f, 2.109f, 3.023f, 4.033f}; // ganhos (divisores compensados)
static float oMv[4]  = {0.0f, 0.0f, 0.0f, 0.0f};         // offsets aditivos por canal (mV)
static bool isInitialized = false;

void ADS_setKDiv(const float *k) {
    if (k != nullptr) {
        memcpy(kDiv, k, 4 * sizeof(float));
    }
}
void ADS_setOffsetMv(const float *o) {
    if (o != nullptr) {
        memcpy(oMv, o, 4 * sizeof(float));
    }
}
void ADS_setCalib(const float *k, const float *o) {
    ADS_setKDiv(k);
    ADS_setOffsetMv(o);
}

static bool readSafe(uint8_t ch, int16_t &val) {
    if (!isInitialized) {
        Serial.println("[ADS] ADC não inicializado");
        return false;
    }
    for (uint8_t t = 0; t < 3; ++t) {
        val = ads.readADC_SingleEnded(ch);
        if (val != 0xFFFF) return true;
        delay(2);
        // tenta reinicializar I2C/ADS
        Wire.begin(42, 41, 50000);
        if (!ads.begin(0x48)) {
            Serial.println("[ADS] Falha ao reinicializar ADC");
            continue;
        }
        ads.setGain(GAIN_TWOTHIRDS);
    }
    Serial.printf("[ADS] Falha na leitura do canal %d\n", ch);
    return false;
}

bool ADS_init() {
    Wire.begin(42, 41, 50000); // 50 kHz I2C
    if (!ads.begin(0x48)) {
        Serial.println("[ADS] Falha ao inicializar ADC");
        isInitialized = false;
        return false;
    }
    ads.setGain(GAIN_TWOTHIRDS); // ±6.144V range
    isInitialized = true;
    return true;
}

// Converte tensão (mV) -> SoC (%)
static uint8_t voltageToSoc(uint16_t mv) {
    const float V_MAX = 4200.0f;
    const float V_MIN = 3200.0f;
    float voltage = (float)mv;
    float soc_f = ((voltage - V_MIN) * 100.0f) / (V_MAX - V_MIN);
    soc_f = fmaxf(0.0f, fminf(100.0f, soc_f));
    return (uint8_t)soc_f;
}

bool ADS_getSample(CellSample &out) {
    if (!isInitialized) {
        Serial.println("[ADS] ADC não inicializado");
        return false;
    }

    int32_t acc[4] = {0};
    int16_t raw;
    uint8_t validSamples = 0;

    for (uint8_t i = 0; i < N; i++) {
        bool sampleValid = true;
        for (uint8_t ch = 0; ch < 4; ch++) {
            if (!readSafe(ch, raw)) {
                sampleValid = false;
                break;
            }
            acc[ch] += raw;
        }
        if (sampleValid) validSamples++;
        delayMicroseconds(125);
    }

    if (validSamples < N/2) {
        Serial.printf("[ADS] Muitas amostras inválidas: %d/%d\n", validSamples, N);
        return false;
    }

    out.epochMs = millis();

    // Tensões absolutas cumulativas (mV) já com ganho e offset
    uint16_t vAbs[4];
    const uint16_t minV[4] = {3400, 6800, 10200, 13600};
    const uint16_t maxV[4] = {4200, 8400, 12600, 16800};

    for (uint8_t ch = 0; ch < 4; ch++) {
        float avg = (float)acc[ch] / validSamples;
        float v   = avg * LSB * kDiv[ch] + oMv[ch]; // aplica offset aditivo por canal
        if (v < 0) v = 0;
        vAbs[ch] = (uint16_t)(v + 0.5f); // arredonda
        if (vAbs[ch] < minV[ch] || vAbs[ch] > maxV[ch]) {
            Serial.printf("[ADS] Tensão absoluta suspeita no canal %d: %dmV\n", ch+1, vAbs[ch]);
        }
    }

    // Diferenciais de célula
    out.mv[0] = vAbs[0];
    out.mv[1] = vAbs[1] - vAbs[0];
    out.mv[2] = vAbs[2] - vAbs[1];
    out.mv[3] = vAbs[3] - vAbs[2];
    out.total = vAbs[3];

    for (int i = 0; i < 4; i++) {
        out.soc[i] = voltageToSoc(out.mv[i]);
    }
    return true;
}

bool ADS_raw(int16_t *arr) {
    if (!arr) return false;
    for (uint8_t i = 0; i < 4; i++) {
        if (!readSafe(i, arr[i])) {
            return false;
        }
    }
    return true;
}
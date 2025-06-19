#pragma once
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

struct CellSample {
    uint32_t epochMs;    // Timestamp em milissegundos
    uint16_t mv[4];      // Tensões das células em mV
    uint16_t total;      // Tensão total do pack
};

// Inicializa o ADC
// Retorna true se bem sucedido, false em caso de erro
bool ADS_init();

// Obtém uma amostra das tensões das células
// Retorna true se bem sucedido, false em caso de erro
bool ADS_getSample(CellSample &out);

// Obtém valores brutos do ADC
// Retorna true se bem sucedido, false em caso de erro
bool ADS_raw(int16_t *arr);

// Define os fatores de divisão de tensão para cada canal
void ADS_setKDiv(const float *k);

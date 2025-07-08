#pragma once
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

/**
 * Estrutura para armazenar uma amostra das células.
 */
struct CellSample {
    uint32_t epochMs;    // Timestamp em milissegundos
    uint16_t mv[4];      // Tensões das células em mV
    uint8_t  soc[4];     // SoC de cada célula em %
    uint16_t total;      // Tensão total do pack
};

/**
 * Inicializa o ADC.
 * @return true se bem sucedido, false em caso de erro.
 */
bool ADS_init();

/**
 * Obtém uma amostra das tensões das células.
 * @param out Estrutura para armazenar a amostra.
 * @return true se bem sucedido, false em caso de erro.
 */
bool ADS_getSample(CellSample &out);

/**
 * Obtém valores brutos do ADC.
 * @param arr Array para armazenar os valores brutos.
 * @return true se bem sucedido, false em caso de erro.
 */
bool ADS_raw(int16_t *arr);

/**
 * Define os fatores de divisão de tensão para cada canal.
 * @param k Array de fatores de divisão.
 */
void ADS_setKDiv(const float *k);

#pragma once
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

struct CellSample {
    uint32_t epochMs;    // millis() da coleta
    uint16_t mv[4];      // tensões individuais (mV)
    uint16_t total;      // pack (mV)
};

bool  ADS_begin(uint8_t sda, uint8_t scl, uint32_t hz = 50000);
void  ADS_setDividers(const float k[4]);
bool  ADS_getSample(CellSample &out);   // oversampling interno

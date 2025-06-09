#pragma once
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

struct CellSample {
    uint32_t epochMs;
    uint16_t mv[4];
    uint16_t total;
};

bool ADS_init();
CellSample ADS_read();
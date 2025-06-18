#pragma once
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

struct CellSample{ uint32_t epochMs; uint16_t mv[4]; uint16_t total; };

void ADS_init();
bool ADS_getSample(CellSample &out);
void ADS_raw(int16_t *arr);
void ADS_setKDiv(const float *k);

#pragma once
#include <Arduino.h>
#include <SPIFFS.h>
#include "ads_driver.h"

bool  FS_init();
void  FS_appendCsv(const CellSample &s);
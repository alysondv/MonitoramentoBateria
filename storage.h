#pragma once
#include "ads_driver.h"
#include <FS.h>
#include <SPIFFS.h>

bool FS_init();
void FS_appendCsv(const CellSample &s);
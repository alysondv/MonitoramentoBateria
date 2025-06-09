#pragma once
#include "ads_driver.h"
#include "storage.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

bool NET_init();
void NET_tick(const CellSample &s);
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "ads_driver.h"

bool NET_init(const char* ssid, const char* pwd);
void NET_tick(const CellSample &s);   // envia WS a cada chamada
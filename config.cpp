#include "config.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

bool CFG_load(Calib &c) {
    File f = SPIFFS.open("/config.json");
    if (!f) return false;
    
    StaticJsonDocument<128> d;
    deserializeJson(d, f);
    for (int i = 0; i < 4; i++) {
        c.kDiv[i] = d["k"][i] | c.kDiv[i];
    }
    return true;
}

void CFG_save(const Calib &c) {
    StaticJsonDocument<128> d;
    for (int i = 0; i < 4; i++) {
        d["k"][i] = c.kDiv[i];
    }
    File f = SPIFFS.open("/config.json", "w");
    serializeJson(d, f);
}
#include "config.h"
#include <SPIFFS.h>
#include <Arduino.h>
#include <ArduinoJson.h>

bool CFG_load(Calib &c) {
    File f = SPIFFS.open("/config.json");
    if (!f) return false;
    if (f.size() == 0) { f.close(); return false; }

    StaticJsonDocument<384> doc;
    DeserializationError error = deserializeJson(doc, f);
    f.close();
    if (error) {
        Serial.print(F("[CFG] Parse erro: "));
        Serial.println(error.c_str());
        return false;
    }

    // carrega kDiv
    JsonArray k_array = doc["k"];
    if (!k_array.isNull()) {
        for (int i = 0; i < 4; i++) {
            JsonVariant v = k_array[i];
            if (!v.isNull()) c.kDiv[i] = v.as<float>();
        }
    }

    // carrega offsets (opcional)
    for (int i = 0; i < 4; i++) c.oMv[i] = 0.0f; // default
    JsonArray o_array = doc["o"];
    if (!o_array.isNull()) {
        for (int i = 0; i < 4; i++) {
            JsonVariant v = o_array[i];
            if (!v.isNull()) c.oMv[i] = v.as<float>();
        }
    }
    return true;
}

void CFG_save(const Calib &c) {
    StaticJsonDocument<256> d;

    JsonArray k_array = d.createNestedArray("k");
    for (int i = 0; i < 4; i++) k_array.add(c.kDiv[i]);

    JsonArray o_array = d.createNestedArray("o");
    for (int i = 0; i < 4; i++) o_array.add(c.oMv[i]);

    File f = SPIFFS.open("/config.json", "w");
    if (!f) {
        Serial.println(F("[CFG] Erro ao abrir config.json para escrita"));
        return;
    }

    serializeJson(d, f);
    f.close();
}
#include <SPIFFS.h>
#include <WiFi.h>
#include "ads_driver.h"
#include "storage.h"
#include "config.h"
#include "net.h"

// valores padrao de kDiv se /config.json nao existir
Calib calib{{1.043f, 2.114f, 3.022f, 4.039f}};

void setup() {
    Serial.begin(115200);
    Serial.println("\n[MAIN] Iniciando...");

    // Inicialização SPIFFS e calibração em paralelo
    bool spiffsOk = SPIFFS.begin(true);
    bool calibOk = false;
    if (spiffsOk) {
        calibOk = CFG_load(calib);
        if (!calibOk) Serial.println("[MAIN] Aviso: Usando valores de calibração padrão");
        ADS_setKDiv(calib.kDiv);
    } else {
        Serial.println("[MAIN] Erro fatal: Falha ao montar SPIFFS");
        while (1) delay(1000);
    }

    // Inicialização do ADC
    if (!ADS_init()) {
        Serial.println("[MAIN] Erro fatal: Falha ao inicializar ADC");
        while (1) delay(1000);
    }

    // Inicialização do sistema de arquivos para logs
    if (!FS_init()) {
        Serial.println("[MAIN] Erro fatal: Falha ao inicializar sistema de arquivos");
        while (1) delay(1000);
    }

    // Inicialização WiFi e servidor em background
    Serial.println("[MAIN] Inicializando WiFi e servidor...");
    NET_init();
    Serial.println("[MAIN] Inicialização completa");
}

void loop() {
    static uint32_t last = 0;
    static uint32_t errorCount = 0;
    static bool timeOk = false;
    CellSample s;

    // Amostragem a 2Hz
    if (millis() - last >= 500) {
        last = millis();
        time_t now = time(nullptr);
        if (now > 1600000000) {
            timeOk = true;
        }
        if (!timeOk) {
            Serial.println("[MAIN] Aguardando sincronização do tempo NTP...");
            return;
        }
        if (!ADS_getSample(s)) {
            errorCount++;
            Serial.printf("[MAIN] Erro na leitura do ADC (%d erros)\n", errorCount);
            if (errorCount > 10) {
                Serial.println("[MAIN] Muitos erros consecutivos, reiniciando...");
                ESP.restart();
            }
            return;
        }
        errorCount = 0;  // Reseta o contador de erros
        if (!FS_appendCsv(s)) {
            Serial.println("[MAIN] Erro ao salvar dados");
        }
        NET_tick(s);
        // Output CSV with real time (UTC-3)
        struct tm tm;
        localtime_r(&now,&tm);
        Serial.printf("%02d:%02d:%02d,%u,%u,%u,%u,%u\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            s.mv[0], s.mv[1], s.mv[2], s.mv[3], s.total);
    }
}
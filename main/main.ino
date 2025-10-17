#include <SPIFFS.h>
#include <WiFi.h>
#include "ads_driver.h"
#include "storage.h"
#include "config.h"
#include "net.h"

Calib calib{{1.043f, 2.114f, 3.022f, 4.039f}, {0.0f, 0.0f, 0.0f, 0.0f}};  // valores padrão

void handleFatalError(const char* message) {
    Serial.println(message);
    Serial.println("[MAIN] Reiniciando o sistema em 5 segundos...");
    delay(5000);
    ESP.restart();
}

void setup() {
    delay(300);
    Serial.begin(115200);
    Serial.println("\n[MAIN] Iniciando...");

    // Inicializa SPIFFS
    bool spiffsOk = SPIFFS.begin(true);
    bool calibOk = false;
    if (spiffsOk) {
        calibOk = CFG_load(calib);
        if (!calibOk) {
            Serial.println("[MAIN] Aviso: Usando valores de calibração padrão.");
        }
        ADS_setCalib(calib.kDiv, calib.oMv);
    } else {
        handleFatalError("[MAIN] Erro fatal: Falha ao montar SPIFFS");
    }

    // Inicializa ADC
    if (!ADS_init()) {
        handleFatalError("[MAIN] Erro fatal: Falha ao inicializar ADC");
    }

    // Inicializa arquivo de log
    if (!FS_init()) {
        handleFatalError("[MAIN] Erro fatal: Falha ao inicializar o sistema de arquivos de log");
    }

    // Inicializa rede e servidor
    Serial.println("[MAIN] Inicializando WiFi e servidor...");
    NET_init();
    Serial.println("[MAIN] Inicialização completa");
}

void loop() {
    static uint32_t last = 0;
    static uint32_t errorCount = 0;
    static bool timeOk = false;
    CellSample s;

    // Loop de amostragem principal (2Hz)
    if (millis() - last >= 500) {
        last = millis();
        time_t now = time(nullptr);

        // Verifica se o tempo já foi sincronizado via NTP
        if (now > 1600000000) {
            timeOk = true;
        }
        if (!timeOk) {
            Serial.println("[MAIN] Aguardando sincronização do tempo NTP...");
            return;
        }

        // Tenta ler uma nova amostra
        if (!ADS_getSample(s)) {
            errorCount++;
            Serial.printf("[MAIN] Erro na leitura do ADC (%d erros)\n", errorCount);
            if (errorCount > 10) {
                Serial.println("[MAIN] Muitos erros consecutivos, reiniciando...");
                ESP.restart();
            }
            return;
        }
        errorCount = 0;

        // Salva no CSV
        if (!FS_appendCsv(s)) {
            Serial.println("[MAIN] Erro ao salvar dados no CSV");
        }

        // Envia dados via WebSocket
        NET_tick(s);

        // Imprime para debug no monitor serial
        struct tm tm;
        localtime_r(&now, &tm);
        Serial.printf("%02d:%02d:%02d,%u,%u,%u,%u,%u\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            s.mv[0], s.mv[1], s.mv[2], s.mv[3], s.total);
    }
}
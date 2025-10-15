#include <SPIFFS.h>
#include <WiFi.h>
#include "ads_driver.h"
#include "storage.h"
#include "config.h"
#include "net.h"

// Valores de calibração padrão caso o /config.json não exista ou falhe.
Calib calib{{1.043f, 2.114f, 3.022f, 4.039f}};

/**
 * Lida com erros irrecuperáveis durante a inicialização.
 * Imprime a mensagem de erro no serial e força um reinício do sistema.
 * Evita que o dispositivo fique "congelado" em um estado de falha.
 * @param message A mensagem de erro a ser exibida.
 */
void handleFatalError(const char* message) {
    Serial.println(message);
    Serial.println("[MAIN] Reiniciando o sistema em 5 segundos...");
    delay(5000); // Delay para dar tempo de ler o log antes do reboot.
    ESP.restart();
}

void setup() {
    delay(300);
    Serial.begin(115200);
    Serial.println("\n[MAIN] Iniciando...");

    // Tenta inicializar o sistema de arquivos SPIFFS. É essencial para logs e config.
    bool spiffsOk = SPIFFS.begin(true);
    bool calibOk = false;
    if (spiffsOk) {
        // SPIFFS OK, tenta carregar a calibração customizada.
        calibOk = CFG_load(calib);
        if (!calibOk) Serial.println("[MAIN] Aviso: Usando valores de calibração padrão.");
        ADS_setKDiv(calib.kDiv);
    } else {
        // Falha no SPIFFS é um erro fatal. Não podemos continuar sem sistema de arquivos.
        handleFatalError("[MAIN] Erro fatal: Falha ao montar SPIFFS");
    }

    // O ADC ADS1115 precisa ser inicializado com sucesso.
    if (!ADS_init()) {
        // Se o ADC não responde, não há como fazer leituras. Erro fatal.
        handleFatalError("[MAIN] Erro fatal: Falha ao inicializar ADC");
    }

    // Prepara o arquivo de log para gravação.
    if (!FS_init()) {
        // Se não pudermos abrir o arquivo de log, a gravação de dados falhará. Erro fatal.
        handleFatalError("[MAIN] Erro fatal: Falha ao inicializar o sistema de arquivos de log");
    }

    // Se todos os sistemas críticos estão OK, inicializa a rede.
    Serial.println("[MAIN] Inicializando WiFi e servidor...");
    NET_init();
    Serial.println("[MAIN] Inicialização completa");
}

void loop() {
    static uint32_t last = 0;
    static uint32_t errorCount = 0;
    static bool timeOk = false;
    CellSample s;

    // Loop de amostragem principal, roda a 2Hz.
    if (millis() - last >= 500) {
        last = millis();
        time_t now = time(nullptr);

        // Só começa a registrar dados depois que o tempo NTP for sincronizado.
        if (now > 1600000000) {
            timeOk = true;
        }
        if (!timeOk) {
            Serial.println("[MAIN] Aguardando sincronização do tempo NTP...");
            return;
        }

        // Tenta obter uma nova amostra do ADC.
        if (!ADS_getSample(s)) {
            errorCount++;
            Serial.printf("[MAIN] Erro na leitura do ADC (%d erros)\n", errorCount);
            // Se o ADC falhar muitas vezes seguidas, algo está errado. Reinicia pra tentar recuperar.
            if (errorCount > 10) {
                Serial.println("[MAIN] Muitos erros consecutivos, reiniciando...");
                ESP.restart();
            }
            return;
        }
        // Leitura bem-sucedida, reseta o contador de erros.
        errorCount = 0;
        
        // Salva a nova amostra no arquivo CSV.
        if (!FS_appendCsv(s)) {
            Serial.println("[MAIN] Erro ao salvar dados no CSV");
        }

        // Envia a amostra via WebSocket para a interface web.
        NET_tick(s);
        
        // Imprime os dados no monitor serial para debug.
        struct tm tm;
        localtime_r(&now,&tm);
        Serial.printf("%02d:%02d:%02d,%u,%u,%u,%u,%u\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            s.mv[0], s.mv[1], s.mv[2], s.mv[3], s.total);
    }
}
#include "storage.h"
#include <SPIFFS.h>

static File logFile;

static bool openLog(bool writeHeader) {
    logFile = SPIFFS.open("/log.csv", FILE_APPEND);
    if (!logFile) {
        Serial.println("[FS] Erro ao abrir/criar /log.csv");
        return false;
    }
    // Escreve cabeçalho se o arquivo está vazio
    if (logFile.size() == 0 && writeHeader) {
        logFile.println("hora,c1_mv,c1_soc,c2_mv,c2_soc,c3_mv,c3_soc,c4_mv,c4_soc,total_mv");
        logFile.flush();
    }
    return true;
}

bool FS_init() {
    // SPIFFS já deve estar montado no setup()
    if (!openLog(true)) {
        Serial.println("[FS] Arquivo de log não disponível");
        return false;
    }
    Serial.println("[FS] Log pronto para uso");
    return true;
}

bool FS_appendCsv(const CellSample &s) {
    if (!logFile) {
        Serial.println("[FS] Log não aberto, tentando reabrir...");
        if (!openLog(false)) return false;
    }
    // Rotaciona log se necessário
    if (logFile.size() > 512000) {
        logFile.close();
        if (SPIFFS.exists("/log_old.csv")) SPIFFS.remove("/log_old.csv");
        SPIFFS.rename("/log.csv", "/log_old.csv");
        if (!openLog(true)) return false;
    }
    // Escreve linha de dados
    time_t now = time(nullptr);
    if (now < 1600000000) {
        Serial.println("[FS] Tempo do sistema inválido");
        return false;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    size_t bytesWritten = logFile.printf("%02d:%02d:%02d,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        s.mv[0], s.soc[0], s.mv[1], s.soc[1],
        s.mv[2], s.soc[2], s.mv[3], s.soc[3],
        s.total);
    logFile.flush();
    if (bytesWritten == 0) {
        Serial.println("[FS] Erro ao escrever no log");
        return false;
    }
    return true;
}

bool FS_clearLogs() {
    if (logFile) logFile.close();
    if (SPIFFS.remove("/log.csv")) {
        Serial.println("[FS] Log apagado");
        return openLog(true);
    }
    Serial.println("[FS] Falha ao apagar log");
    return false;
}
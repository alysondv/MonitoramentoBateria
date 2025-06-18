#include "storage.h"
#include <SPIFFS.h>

static File logFile;
static void openLog(bool hdr){ logFile = SPIFFS.open("/log.csv",FILE_APPEND); if(hdr) logFile.println("Time,C1,C2,C3,C4,Total"); }

void FS_init(){ SPIFFS.begin(true); if(!SPIFFS.exists("/log.csv")) openLog(true); else openLog(false);} 

void FS_appendCsv(const CellSample &s){
    if(!logFile) return;

    /* rotação */
    if(logFile.size() > 512000){
        logFile.close();
        SPIFFS.remove("/log_old.csv");
        SPIFFS.rename("/log.csv","/log_old.csv");
        openLog(true);
    }

    /* horário NTP local (UTC-3) */
    time_t now = time(nullptr);
    struct tm tm;  localtime_r(&now,&tm);

    logFile.printf("%02d:%02d:%02d,%u,%u,%u,%u,%u\n",
                   tm.tm_hour, tm.tm_min, tm.tm_sec,
                   s.mv[0], s.mv[1], s.mv[2], s.mv[3], s.total);
    logFile.flush();
}

void FS_clearLogs(){ logFile.close(); SPIFFS.remove("/log.csv"); openLog(true); }
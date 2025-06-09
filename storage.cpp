#include "storage.h"
#include <time.h>

static File logFile;

bool FS_init(){
    if(!SPIFFS.begin(true)) return false;
    if(!SPIFFS.exists("/log.csv")){
        File f=SPIFFS.open("/log.csv", FILE_WRITE);
        f.println("Time,C1,C2,C3,C4,Total");
        f.close();
    }
    logFile = SPIFFS.open("/log.csv", FILE_APPEND);
    return logFile;
}

static String ts(uint32_t ms){
    char b[9]; struct tm t;
    if(!getLocalTime(&t,50)) strcpy(b,"00:00:00");
    else strftime(b,9,"%H:%M:%S",&t);
    return String(b);
}

void FS_appendCsv(const CellSample &s){
    if(!logFile) return;
    if(logFile.size()>512000){
        logFile.close();
        if(SPIFFS.exists("/log_old.csv")) SPIFFS.remove("/log_old.csv");
        SPIFFS.rename("/log.csv","/log_old.csv");
        logFile = SPIFFS.open("/log.csv", FILE_WRITE);
        logFile.println("Time,C1,C2,C3,C4,Total");
    }
    logFile.printf("%s,%u,%u,%u,%u,%u\n", ts(s.epochMs).c_str(), s.mv[0],s.mv[1],s.mv[2],s.mv[3],s.total);
    logFile.flush();
}

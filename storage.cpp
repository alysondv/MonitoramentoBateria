#include "storage.h"
static File logFile;

static String ts(uint32_t ms){
    time_t t=time(NULL);
    struct tm *lt=localtime(&t);
    char buf[9];
    sprintf(buf,"%02d:%02d:%02d",lt->tm_hour,lt->tm_min,lt->tm_sec);
    return String(buf);
}

bool FS_init(){
    if(!SPIFFS.begin(true)) return false;
    if(!SPIFFS.exists("/log.csv")){
        logFile = SPIFFS.open("/log.csv", FILE_WRITE);
        logFile.println("Time,C1,C2,C3,C4,Total");
    }else logFile = SPIFFS.open("/log.csv", FILE_APPEND);
    return logFile;
}

void FS_appendCsv(const CellSample &s){
    if(!logFile) return;
    if(logFile.size()>512000){
        logFile.close();
        SPIFFS.rename("/log.csv","/log_old.csv");
        logFile = SPIFFS.open("/log.csv", FILE_WRITE);
        logFile.println("Time,C1,C2,C3,C4,Total");
    }
    logFile.printf("%s,%u,%u,%u,%u,%u\n", ts(s.epochMs).c_str(), s.mv[0],s.mv[1],s.mv[2],s.mv[3],s.total);
    logFile.flush();
}
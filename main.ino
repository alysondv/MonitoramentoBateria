#include "ads_driver.h"
#include "storage.h"
#include "net.h"

const char* SSID="Mocoto";
const char* PASS="gleja23#";
float kDivUser[4]={1.042f,2.109f,3.023f,4.033f};

void setup(){
    Serial.begin(115200);
    ADS_begin(42,41,50000);
    ADS_setDividers(kDivUser);
    FS_init();
    NET_init(SSID,PASS);
    Serial.println("Time,C1,C2,C3,C4,Total");
}

void loop(){
    static uint32_t last=0;
    if(millis()-last<500) return;
    last=millis();
    CellSample s; ADS_getSample(s);
    Serial.printf("%02u:%02u:%02u,%u,%u,%u,%u,%u\n",(s.epochMs/3600000)%24,(s.epochMs/60000)%60,(s.epochMs/1000)%60,
                 s.mv[0],s.mv[1],s.mv[2],s.mv[3],s.total);
    FS_appendCsv(s);
    NET_tick(s);
}
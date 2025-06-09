#include "ads_driver.h"
#include "storage.h"
#include "net.h"

void setup(){
    Serial.begin(115200);
    ADS_init();
    FS_init();
    NET_init();
}

void loop(){
    static uint32_t last=0;
    if(millis()-last>=500){
        last=millis();
        CellSample s=ADS_read();
        Serial.printf("%u,%u,%u,%u,%u,%u\n",s.epochMs,s.mv[0],s.mv[1],s.mv[2],s.mv[3],s.total);
        FS_appendCsv(s);
        NET_tick(s);
    }
}

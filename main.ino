#include <SPIFFS.h>
#include <WiFi.h>
#include "ads_driver.h"
#include "storage.h"
#include "config.h"
#include "net.h"

// valores-default de kDiv caso ainda não exista /config.json
Calib calib{{1.043f, 2.114f, 3.022f, 4.039f}};

void setup(){
    Serial.begin(115200);
    // Inicializa SPIFFS e carrega calibração persistente (se houver)
    if(!SPIFFS.begin(true)) while(1);
    CFG_load(calib);               // mantém valores default se arquivo não existir
    ADS_setKDiv(calib.kDiv);

    // Inicializa módulos
    ADS_init();
    FS_init();
    NET_init();
}

void loop(){
    static uint32_t last = 0;
    CellSample s;
    if(millis() - last >= 500){      // 2 Hz
        last = millis();
        if(ADS_getSample(s)){
            FS_appendCsv(s);
            NET_tick(s);

            // Serial CSV com horário real (UTC-3)
            time_t now = time(nullptr);
            struct tm tm; localtime_r(&now,&tm);
            Serial.printf("%02d:%02d:%02d,%u,%u,%u,%u,%u\n",
                          tm.tm_hour, tm.tm_min, tm.tm_sec,
                          s.mv[0], s.mv[1], s.mv[2], s.mv[3], s.total);
        }
    }
}
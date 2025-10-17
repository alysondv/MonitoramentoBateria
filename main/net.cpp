#include "net.h"
#include "web_ui.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "storage.h"
#include "config.h"
#include "ads_driver.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

const char *SSID = "Mocoto";
const char *PASS = "1234567i";
static constexpr long TZ_OFFSET = -3 * 3600;

// Grava index.html no SPIFFS
static void putIndex() {
    File f = SPIFFS.open("/index.html", "w");
    f.print(index_html);
}

void NET_init() {
    SPIFFS.remove("/index.html");
    putIndex();

    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        esp_sleep_enable_timer_wakeup(5 * 60 * 1000000ULL);
        Serial.flush();
        esp_deep_sleep_start();
    }

    configTime(TZ_OFFSET, 0, "pool.ntp.org");
    struct tm tmcheck; time_t now; uint32_t ntpTimeout=millis();
    do{ now=time(nullptr); localtime_r(&now,&tmcheck); }while(now<1600000000 && millis()-ntpTimeout<2000);

    server.on("/", HTTP_GET, [](auto *r){ r->send(SPIFFS, "/index.html", "text/html"); });
    server.on("/download", HTTP_GET, [](auto *r){ r->send(SPIFFS, "/log.csv", "text/csv", true); });
    server.on("/api/raw", HTTP_GET, [](auto *r){
        int16_t raw[4];
        ADS_raw(raw);
        StaticJsonDocument<128> d;
        for(int i=0;i<4;i++) d["raw"][i]=raw[i];
        d["lsb"]=0.1875;
        String o; serializeJson(d,o);
        r->send(200,"application/json",o);
    });

    // Calibração: aceita um ponto (ajusta offsets) ou dois pontos (ajusta ganho e offset)
    server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        String body((char*)data, len);
        StaticJsonDocument<256> d;
        if (deserializeJson(d, body)) {
            request->send(400, "text/plain", "Erro de JSON");
            return;
        }

        JsonArray v_cells1 = d["v"];
        JsonArray v_cells2 = d["v2"]; // opcional
        if (v_cells1.isNull() || v_cells1.size()!=4) {
            request->send(400,"text/plain","Payload inválido: 'v'");
            return;
        }

        // Leitura RAW ponto 1
        int16_t r1[4];
        if (!ADS_raw(r1)) { request->send(500,"text/plain","Falha ao ler RAW"); return; }

        // Cumulativos mV do ponto 1
        float cum1[4]; float acc=0.0f;
        for (int i=0;i<4;i++){ acc += (float)v_cells1[i] * 1000.0f; cum1[i]=acc; }

        // Carrega calib atual como base
        Calib cur;
        if (!CFG_load(cur)) {
            cur = Calib{{1.043f, 2.114f, 3.022f, 4.039f}, {0,0,0,0}};
        }
        Calib outCal = cur;
        const float lsb = 0.1875f;

        if (!v_cells2.isNull() && v_cells2.size()==4) {
            // Dois pontos: estima k e offset por canal (reta V = k*R*lsb + o)
            delay(5);
            int16_t r2[4];
            if (!ADS_raw(r2)) { request->send(500,"text/plain","Falha ao ler RAW (ponto 2)"); return; }
            float cum2[4]; float acc2=0.0f;
            for (int i=0;i<4;i++){ acc2 += (float)v_cells2[i]*1000.0f; cum2[i]=acc2; }

            for (int i=0;i<4;i++){
                if (r2[i]==r1[i]) {
                    // degenerado: ajusta só offset com base no ponto 1
                    float v_est = cur.kDiv[i]*r1[i]*lsb + cur.oMv[i];
                    outCal.kDiv[i] = cur.kDiv[i];
                    outCal.oMv[i]  = cum1[i] - v_est;
                } else {
                    float k = (cum2[i]-cum1[i]) / ((r2[i]-r1[i]) * lsb);
                    float o = cum1[i] - k * r1[i] * lsb;
                    outCal.kDiv[i]=k; outCal.oMv[i]=o;
                }
            }
        } else {
            // Um ponto: mantém k e ajusta offsets para alinhar cumulativos
            for (int i=0;i<4;i++){
                float v_est = cur.kDiv[i]*r1[i]*lsb + cur.oMv[i];
                outCal.oMv[i] = cum1[i] - v_est;
            }
        }

        ADS_setCalib(outCal.kDiv, outCal.oMv);
        CFG_save(outCal);
        Serial.println("[CALIB] Nova calibração salva:");
        for (int i = 0; i < 4; i++) {
            Serial.printf("  kDiv[%d]=%.6f  oMv[%d]=%.2f\n", i, outCal.kDiv[i], i, outCal.oMv[i]);
        }
        request->send(200, "text/plain", "Calibração aplicada (ganho+offset)");
    });

    server.on("/api/clear_logs", HTTP_POST, [](auto *r){
        FS_clearLogs();
        r->send(200, "text/plain", "CLEARED");
    });

    ws.onEvent([](AsyncWebSocket *srv, AsyncWebSocketClient *cli, AwsEventType type, void *arg, uint8_t *data, size_t len){
        if(type==WS_EVT_CONNECT) Serial.println("[WS] cliente conectado");
    });
    server.addHandler(&ws);
    server.begin();

    Serial.printf("[NET] Server pronto – acesse: http://%s\n", WiFi.localIP().toString().c_str());
}

void NET_tick(const CellSample &s) {
    StaticJsonDocument<256> d;
    char tbuf[9];
    snprintf(tbuf,9,"%02u:%02u:%02u",(s.epochMs/3600000)%24,(s.epochMs/60000)%60,(s.epochMs/1000)%60);
    d["t"] = tbuf;
    JsonArray v_arr = d.createNestedArray("v");
    JsonArray soc_arr = d.createNestedArray("soc");
    for (uint8_t i = 0; i < 4; i++) {
        v_arr.add(s.mv[i]);
        soc_arr.add(s.soc[i]);
    }
    d["tot"] = s.total;
    String o;
    serializeJson(d, o);
    ws.textAll(o);
}
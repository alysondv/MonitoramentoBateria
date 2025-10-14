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

/* ----------------------------------------------------------
   Single‑page HTML: Monitor + Setup
   ---------------------------------------------------------- */

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
    server.on("/api/raw", HTTP_GET, [](auto *r){ int16_t raw[4]; ADS_raw(raw); StaticJsonDocument<128> d; for(int i=0;i<4;i++) d["raw"][i]=raw[i]; d["lsb"]=0.1875; String o; serializeJson(d,o); r->send(200,"application/json",o); });

    server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            String body((char*)data, len);
            StaticJsonDocument<128> d;
            DeserializationError err = deserializeJson(d, body);
            if (err) {
                Serial.println("[CALIB] Erro ao interpretar JSON");
                request->send(400, "text/plain", "Erro de JSON");
                return;
            }

            Calib novaCalib;
            int16_t raw[4];
            ADS_raw(raw);
            JsonArray v = d["v"];
            bool ok = true;
            for (int i = 0; i < 4; i++) {
                float vReal = v[i];
                if (vReal < 3000 || vReal > 20000 || raw[i] <= 0 || raw[i] > 32000) {
                    Serial.printf("[CALIB] Valor inválido em C%d: v=%.2f, raw=%d\n", i+1, vReal, raw[i]);
                    ok = false;
                    break;
                }
                novaCalib.kDiv[i] = vReal / (raw[i] * 0.1875f);
            }
            if (ok) {
                ADS_setKDiv(novaCalib.kDiv);
                CFG_save(novaCalib);
                Serial.println("[CALIB] Calibração aplicada com sucesso:");
                for (int i = 0; i < 4; i++) Serial.printf("  kDiv[%d] = %.6f\n", i, novaCalib.kDiv[i]);
                request->send(200, "text/plain", "Calibração aplicada");
            } else {
                Serial.println("[CALIB] Calibração abortada – valores inválidos");
                request->send(422, "text/plain", "Valores inválidos");
            }
        });

    server.on("/api/clear_logs", HTTP_POST, [](auto *r){ FS_clearLogs(); r->send(200, "text/plain", "CLEARED"); });
    ws.onEvent([](AsyncWebSocket *srv, AsyncWebSocketClient *cli, AwsEventType type, void *arg, uint8_t *data, size_t len){ if(type==WS_EVT_CONNECT) Serial.println("[WS] cliente conectado"); });
    server.addHandler(&ws);
    server.begin();

    Serial.printf("[NET] Server pronto – acesse: http://%s\n", WiFi.localIP().toString().c_str());
}

void NET_tick(const CellSample &s) {
    StaticJsonDocument<256> d; // Aumentado para 256
    char tbuf[9];
    snprintf(tbuf,9,"%02u:%02u:%02u",(s.epochMs/3600000)%24,(s.epochMs/60000)%60,(s.epochMs/1000)%60);
    d["t"] = tbuf;
    JsonArray v_arr = d.createNestedArray("v");
    JsonArray soc_arr = d.createNestedArray("soc"); // Adicionado
    for (uint8_t i = 0; i < 4; i++) {
        v_arr.add(s.mv[i]);
        soc_arr.add(s.soc[i]); // Adicionado
    }
    d["tot"] = s.total;
    String o;
    serializeJson(d, o);
    ws.textAll(o);
}
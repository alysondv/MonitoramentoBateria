# one_file_project.md

---
## main.ino
```arduino
#include <SPIFFS.h>
#include <WiFi.h>
#include "ads_driver.h"
#include "storage.h"
#include "config.h"
#include "net.h"

// Default kDiv values if /config.json doesn't exist
Calib calib{{1.043f, 2.114f, 3.022f, 4.039f}};

void setup() {
    Serial.begin(115200);
    Serial.println("\n[MAIN] Iniciando...");

    // Inicialização SPIFFS e calibração em paralelo
    bool spiffsOk = SPIFFS.begin(true);
    bool calibOk = false;
    if (spiffsOk) {
        calibOk = CFG_load(calib);
        if (!calibOk) Serial.println("[MAIN] Aviso: Usando valores de calibração padrão");
        ADS_setKDiv(calib.kDiv);
    } else {
        Serial.println("[MAIN] Erro fatal: Falha ao montar SPIFFS");
        while (1) delay(1000);
    }

    // Inicialização do ADC
    if (!ADS_init()) {
        Serial.println("[MAIN] Erro fatal: Falha ao inicializar ADC");
        while (1) delay(1000);
    }

    // Inicialização do sistema de arquivos para logs
    if (!FS_init()) {
        Serial.println("[MAIN] Erro fatal: Falha ao inicializar sistema de arquivos");
        while (1) delay(1000);
    }

    // Inicialização WiFi e servidor em background
    Serial.println("[MAIN] Inicializando WiFi e servidor...");
    NET_init();
    Serial.println("[MAIN] Inicialização completa");
}

void loop() {
    static uint32_t last = 0;
    static uint32_t errorCount = 0;
    static bool timeOk = false;
    CellSample s;

    // Sample at 2Hz
    if (millis() - last >= 500) {
        last = millis();
        time_t now = time(nullptr);
        if (now > 1600000000) {
            timeOk = true;
        }
        if (!timeOk) {
            Serial.println("[MAIN] Aguardando sincronização do tempo NTP...");
            return;
        }
        if (!ADS_getSample(s)) {
            errorCount++;
            Serial.printf("[MAIN] Erro na leitura do ADC (%d erros)\n", errorCount);
            if (errorCount > 10) {
                Serial.println("[MAIN] Muitos erros consecutivos, reiniciando...");
                ESP.restart();
            }
            return;
        }
        errorCount = 0;  // Reset error counter on success
        if (!FS_appendCsv(s)) {
            Serial.println("[MAIN] Erro ao salvar dados");
        }
        NET_tick(s);
        // Output CSV with real time (UTC-3)
        struct tm tm;
        localtime_r(&now,&tm);
        Serial.printf("%02d:%02d:%02d,%u,%u,%u,%u,%u\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            s.mv[0], s.mv[1], s.mv[2], s.mv[3], s.total);
    }
}
```

---
## ads_driver.h
```cpp
#pragma once
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

/**
 * Estrutura para armazenar uma amostra das células.
 */
struct CellSample {
    uint32_t epochMs;    // Timestamp em milissegundos
    uint16_t mv[4];      // Tensões das células em mV
    uint8_t  soc[4];     // SoC de cada célula em %
    uint16_t total;      // Tensão total do pack
};

/**
 * Inicializa o ADC.
 * @return true se bem sucedido, false em caso de erro.
 */
bool ADS_init();

/**
 * Obtém uma amostra das tensões das células.
 * @param out Estrutura para armazenar a amostra.
 * @return true se bem sucedido, false em caso de erro.
 */
bool ADS_getSample(CellSample &out);

/**
 * Obtém valores brutos do ADC.
 * @param arr Array para armazenar os valores brutos.
 * @return true se bem sucedido, false em caso de erro.
 */
bool ADS_raw(int16_t *arr);

/**
 * Define os fatores de divisão de tensão para cada canal.
 * @param k Array de fatores de divisão.
 */
void ADS_setKDiv(const float *k);
```

---
## ads_driver.cpp
```cpp
#include "ads_driver.h"
#include <Wire.h>
#include <math.h>

static Adafruit_ADS1115 ads;
static constexpr uint8_t N = 8;           // Oversampling count
static constexpr float LSB = 0.1875f;     // mV/bit @ ±6.144V
static float kDiv[4] = {1.042f, 2.109f, 3.023f, 4.033f};
static bool isInitialized = false;

void ADS_setKDiv(const float *k) {
    if (k != nullptr) {
        memcpy(kDiv, k, 4 * sizeof(float));
    }
}

static bool readSafe(uint8_t ch, int16_t &val) {
    if (!isInitialized) {
        Serial.println("[ADS] ADC não inicializado");
        return false;
    }
    
    for (uint8_t t = 0; t < 3; ++t) {
        val = ads.readADC_SingleEnded(ch);
        if (val != 0xFFFF) return true;
        delay(2);
        
        // Tenta reinicializar o ADS em caso de erro
        Wire.begin(42, 41, 50000);
        if (!ads.begin(0x48)) {
            Serial.println("[ADS] Falha ao reinicializar ADC");
            continue;
        }
        ads.setGain(GAIN_TWOTHIRDS);
    }
    Serial.printf("[ADS] Falha na leitura do canal %d\n", ch);
    return false;
}

bool ADS_init() {
    Wire.begin(42, 41, 50000);    // 50 kHz I2C
    
    if (!ads.begin(0x48)) {
        Serial.println("[ADS] Falha ao inicializar ADC");
        isInitialized = false;
        return false;
    }
    
    ads.setGain(GAIN_TWOTHIRDS);  // ±6.144V range
    isInitialized = true;
    return true;
}

// Função para converter tensão (mV) em SoC (%)
// Baseado na fórmula: SoC(%) = (V_medida - 3.2) * 100 / (4.2 - 3.2)
static uint8_t voltageToSoc(uint16_t mv) {
    const float V_MAX = 4200.0f;
    const float V_MIN = 3200.0f;
    float voltage = (float)mv;
    float soc_f = ((voltage - V_MIN) * 100.0f) / (V_MAX - V_MIN);
    soc_f = fmaxf(0.0f, fminf(100.0f, soc_f));
    return (uint8_t)soc_f;
}

bool ADS_getSample(CellSample &out) {
    if (!isInitialized) {
        Serial.println("[ADS] ADC não inicializado");
        return false;
    }

    int32_t acc[4] = {0};
    int16_t raw;
    uint8_t validSamples = 0;

    // Accumulate samples for oversampling
    for (uint8_t i = 0; i < N; i++) {
        bool sampleValid = true;
        for (uint8_t ch = 0; ch < 4; ch++) {
            if (!readSafe(ch, raw)) {
                sampleValid = false;
                break;
            }
            acc[ch] += raw;
        }
        if (sampleValid) validSamples++;
        delayMicroseconds(125);
    }

    if (validSamples < N/2) {
        Serial.printf("[ADS] Muitas amostras inválidas: %d/%d\n", validSamples, N);
        return false;
    }

    out.epochMs = millis();
    
    // Calculate absolute voltages
    uint16_t vAbs[4];
    const uint16_t minV[4] = {3400, 6800,  10200, 13600};
    const uint16_t maxV[4] = {4200, 8400, 12600, 16800};
    for (uint8_t ch = 0; ch < 4; ch++) {
        float avg = (float)acc[ch] / validSamples;
        vAbs[ch] = (uint16_t)(avg * LSB * kDiv[ch]);
        
        // Validação básica das tensões absolutas por canal
        if (vAbs[ch] < minV[ch] || vAbs[ch] > maxV[ch]) {
            Serial.printf("[ADS] Tensão absoluta suspeita no canal %d: %dmV\n", ch+1, vAbs[ch]);
        }
    }

    // Calculate differential voltages
    out.mv[0] = vAbs[0];
    out.mv[1] = vAbs[1] - vAbs[0];
    out.mv[2] = vAbs[2] - vAbs[1];
    out.mv[3] = vAbs[3] - vAbs[2];
    out.total = vAbs[3];
    // --- INÍCIO DA ALTERAÇÃO ---
    // Calcula o SoC para cada célula
    for (int i = 0; i < 4; i++) {
        out.soc[i] = voltageToSoc(out.mv[i]);
    }
    // --- FIM DA ALTERAÇÃO ---
    return true;
}

bool ADS_raw(int16_t *arr) {
    if (!arr) return false;
    
    for (uint8_t i = 0; i < 4; i++) {
        if (!readSafe(i, arr[i])) {
            return false;
        }
    }
    return true;
}
```

---
## config.h
```cpp
#pragma once

/**
 * Estrutura de calibração dos divisores de tensão.
 */
struct Calib {
    float kDiv[4];
};

/**
 * Carrega os fatores de calibração do arquivo de configuração.
 * @param c Estrutura para armazenar os fatores.
 * @return true se bem sucedido, false em caso de erro.
 */
bool CFG_load(Calib &c);

/**
 * Salva os fatores de calibração no arquivo de configuração.
 * @param c Estrutura com os fatores a salvar.
 */
void CFG_save(const Calib &c);
```

---
## config.cpp
```cpp
#include "config.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

bool CFG_load(Calib &c) {
    File f = SPIFFS.open("/config.json");
    if (!f) return false;
    
    StaticJsonDocument<128> d;
    deserializeJson(d, f);
    for (int i = 0; i < 4; i++) {
        c.kDiv[i] = d["k"][i] | c.kDiv[i];
    }
    return true;
}

void CFG_save(const Calib &c) {
    File f = SPIFFS.open("/config.json", "w");
    if (!f) return;
    StaticJsonDocument<128> d;
    for (int i = 0; i < 4; i++) d["k"][i] = c.kDiv[i];
    serializeJson(d, f);
    f.close();
}
```

---
## storage.h
```cpp
#pragma once
#include "ads_driver.h"

/**
 * Inicializa o sistema de arquivos.
 */
bool FS_init();

/**
 * Adiciona uma amostra ao arquivo CSV.
 */
bool FS_appendCsv(const CellSample &s);

/**
 * Limpa todos os logs.
 */
bool FS_clearLogs();
```

---
## storage.cpp
```cpp
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
```

---
## net.h
```cpp
#pragma once
#include "ads_driver.h"

void NET_init();
void NET_tick(const CellSample &s);
```

---
## net.cpp
```cpp
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

const char *SSID = "NEST-Shop";
const char *PASS = "NDA1T5D0";
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
```

---
## web_ui.h
```cpp
#pragma once

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>LiPo Monitor</title>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<style>
body { font-family: 'Segoe UI', sans-serif; background: #f4f4f9; margin: 0 auto; max-width: 1000px; padding: 20px; color: #333; }
h2 { text-align: center; }
.tabs { text-align: center; margin-bottom: 20px; }
.tabs button {
  background-color: #2196F3;
  border: none;
  color: white;
  padding: 10px 20px;
  margin: 0 5px;
  border-radius: 6px;
  cursor: pointer;
  transition: background-color 0.3s;
}
.tabs button:hover { background-color: #1565c0; }
.tabs button.active { background-color: #0d47a1; }
.pane { display: none; }
.pane.active { display: block; }
.cards { display: flex; gap: 12px; flex-wrap: wrap; justify-content: center; }
.card { background: white; border-radius: 10px; padding: 12px; width: 140px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); text-align: center; cursor: pointer; transition: transform 0.2s; }
.card:hover { transform: scale(1.05); }
.battery { width: 50px; height: 100px; border: 3px solid #333; border-radius: 6px; position: relative; margin: 8px auto; background: linear-gradient(to top, var(--batt-color) var(--level), #ddd var(--level)); }
.battery::before { content: ""; position: absolute; top: -10px; left: 15px; width: 20px; height: 6px; background: #333; border-radius: 2px; }
.total-battery { border-width: 4px; }
canvas { max-width: 100%; height: 180px; margin-top: 20px; }
a { display: inline-block; margin-top: 12px; color: #2196F3; text-decoration: none; }
a:hover { text-decoration: underline; }
.setup-box { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); max-width: 500px; margin: auto; }
.setup-box h3 { margin-top: 0; }
.setup-box input { width: 60px; padding: 5px; margin: 5px; border: 1px solid #ccc; border-radius: 4px; }
.setup-box button { background-color: #2196F3; color: white; border: none; padding: 8px 16px; border-radius: 6px; margin: 8px 0; cursor: pointer; transition: background-color 0.3s; }
.setup-box button:hover { background-color: #0d47a1; }
</style>
</head>
<body>
<h2>Monitor de Baterias LiPo</h2>
<div class='tabs'>
 <button id='tabMon' class='active'>Monitor</button>
 <button id='tabSet'>Setup</button>
</div>
<div id='paneMon' class='pane active'>
  <div class='cards'>
    <div class='card' onclick="selectCell(0)">C1<div class='battery' id='batt0'></div><span id='v0'>-</span> V | <span id='soc0'>-</span>%</div>
    <div class='card' onclick="selectCell(1)">C2<div class='battery' id='batt1'></div><span id='v1'>-</span> V | <span id='soc1'>-</span>%</div>
    <div class='card' onclick="selectCell(2)">C3<div class='battery' id='batt2'></div><span id='v2'>-</span> V | <span id='soc2'>-</span>%</div>
    <div class='card' onclick="selectCell(3)">C4<div class='battery' id='batt3'></div><span id='v3'>-</span> V | <span id='soc3'>-</span>%</div>
    <div class='card' onclick="selectCell(4)">Total<div class='battery total-battery' id='battTot'></div><span id='tot'>-</span> V</div>
  </div>
  <canvas id='graph'></canvas>
  <a href='/download'>📥 Baixar CSV</a>
</div>
<div id='paneSet' class='pane'>
  <div class="setup-box">
    <h3>⚙️ Calibração kDiv</h3>
    <p>Digite tensões medidas (V):</p>
    <div>🔋 C1 <input id='in0'> V</div>
    <div>🔋 C1+C2 <input id='in1'> V</div>
    <div>🔋 C1+C2+C3 <input id='in2'> V</div>
    <div>🔋 Pack <input id='in3'> V</div>
    <button onclick='calib()'>✅ Calibrar</button>
    <h3>🗑️ Logs</h3>
    <button onclick='clearLog()'>🧹 Limpar logs</button>
  </div>
</div>
<script>
const tabMon=document.getElementById('tabMon'),tabSet=document.getElementById('tabSet');
const paneMon=document.getElementById('paneMon'),paneSet=document.getElementById('paneSet');
function show(p){paneMon.classList.toggle('active',p==='mon');paneSet.classList.toggle('active',p==='set');tabMon.classList.toggle('active',p==='mon');tabSet.classList.toggle('active',p==='set');}
tabMon.onclick=()=>show('mon'); tabSet.onclick=()=>show('set');
let selectedCell = 0;
function selectCell(c){selectedCell = c; updateGraph();}
const ctx = graph.getContext('2d');
const chart = new Chart(ctx, {type:'line',data:{labels:[],datasets:[{label:'Célula',data:[],borderWidth:2,borderColor:'#2196F3',backgroundColor:'rgba(33, 150, 243, 0.2)',pointRadius:2}]},options:{animation:false,scales:{x:{display:false},y:{min:3,max:4.3}}}});
const hist=[[],[],[],[],[]]; const labels=[]; const cells=4;
let ws=new WebSocket('ws://'+location.hostname+'/ws');
ws.onmessage=e=>{
    const d=JSON.parse(e.data); 
    const now = new Date().toLocaleTimeString(); 
    labels.push(now); 
    if(labels.length>60) labels.shift(); 
    d.v.forEach((mv,i)=>{
        const v=mv/1000; 
        document.getElementById('v'+i).textContent = v.toFixed(3); 
        const soc = d.soc[i];
        document.getElementById('soc'+i).textContent = soc;
        updateBattery('batt'+i, v, soc);
        hist[i].push(v); 
        if(hist[i].length>60) hist[i].shift();
    }); 
    const total=d.tot/1000; 
    document.getElementById('tot').textContent=total.toFixed(3); 
    updateBattery('battTot',total, null, true);
    hist[4].push(total); 
    if(hist[4].length>60) hist[4].shift(); 
    updateGraph();
};

function updateBattery(id, v, soc, isTotal=false){
    let p;
    if (isTotal) {
        const cells = 4;
        const min=3.2*cells, max=4.2*cells;
        p = Math.max(0, Math.min(1, (v-min)/(max-min)));
    } else {
        p = soc / 100.0;
    }
    const b=document.getElementById(id);
    b.style.setProperty('--level',(p*100)+'%');
    let c;
    const vpc=isTotal?v/4:v;
    if(vpc>=3.7)c='#4caf50';else if(vpc>=3.5)c='#ffc107';else c='#f44336';
    b.style.setProperty('--batt-color',c);
}
function updateGraph(){chart.data.labels=[...labels];const t=selectedCell===4,n=t?'Total':'C'+(selectedCell+1);chart.data.datasets[0].label=n+' (V)';chart.data.datasets[0].data=[...hist[selectedCell]];chart.options.scales.y=t?{min:3*cells,max:4.2*cells}:{min:3,max:4.3};chart.update();}
function calib(){const v=[0,1,2,3].map(i=>parseFloat(document.getElementById('in'+i).value)*1000||0);const p=JSON.stringify({v});fetch('/api/calibrate',{method:'POST',headers:{'Content-Type':'application/json'},body:p}).then(r=>r.text()).then(t=>alert("Resposta: "+t)).catch(e=>alert("Erro: "+e));}
function clearLog(){fetch('/api/clear_logs',{method:'POST'});}
</script>
</body>
</html>
)rawliteral";
```

---
## partitions.csv
```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x4000,
phy_init, data, phy,     0xd000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
spiffs,   data, spiffs,        ,  2M,
```

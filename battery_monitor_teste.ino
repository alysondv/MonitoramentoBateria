/*
 * Projeto: Monitoramento Avançado de Baterias LiFePO4
 * ESP32-S3-DevKitC-1 + ADS1115
 * Funcionalidades:
 * - Leitura precisa de 4 células (16 bits)
 * - Cálculo de estado de carga
 * - Proteção contra sobretensão
 * - Armazenamento em SPIFFS com rotação
 * - Interface Web/WebSocket
 * - Indicadores visuais com LEDs
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_ADS1X15.h>
#include <SPIFFS.h>

// Configuração do Hardware
#define NUM_BATTERIES 4
const int LED_PINS[NUM_BATTERIES] = {12, 13, 14, 15};
const int I2C_SDA = 17;
const int I2C_SCL = 18;

// Parâmetros das Baterias LiFePO4
const int16_t MIN_VOLTAGE = 2800;    // 2.8V (0%)
const int16_t MAX_VOLTAGE = 3600;    // 3.6V (100%)
const int16_t OVER_VOLTAGE = 3700;   // 3.7V (limite)
const size_t MAX_FILE_SIZE = 512000; // 512KB (50% da partição)

// WiFi
const char* ssid = "SUA_REDE";
const char* password = "SUA_SENHA";

// Intervalos (ms)
const unsigned long READ_INTERVAL = 5000;    // 5s
const unsigned long SAVE_INTERVAL = 300000;  // 5min
const unsigned long RECONNECT_INTERVAL = 30000;

// Objetos Globais
Adafruit_ADS1115 ads;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct BatteryData {
  int16_t voltages[NUM_BATTERIES];
  int8_t percentages[NUM_BATTERIES];
  int16_t totalVoltage;
  int8_t totalPercentage;
  bool overVoltage;
};

// Protótipos
void initHardware();
void initWiFi();
void initSPIFFS();
void initWebServer();
void readBatteries(BatteryData*);
void calculatePercentages(BatteryData*);
void checkSafety(BatteryData*);
void updateLEDs(const BatteryData*);
void saveData(const BatteryData*);
void rotateFiles();
void sendWSData(const BatteryData*);
void onWebSocketEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t);
void debugPrintData(const BatteryData*);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Monitor de Baterias</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; }
    .battery { 
      width: 60px; height: 120px; border: 2px solid #333; 
      position: relative; display: inline-block; margin: 10px;
    }
    .level { 
      position: absolute; bottom: 0; width: 100%; 
      background: #4CAF50; transition: height 0.5s;
    }
    .alarm { background: #f44336 !important; }
    .info { margin-top: 5px; text-align: center; }
    #status { 
      padding: 10px; margin: 10px 0; border-radius: 5px;
      background: #f8f8f8; font-weight: bold;
    }
  </style>
</head>
<body>
  <h1>Monitor de Baterias LiFePO4</h1>
  <div id="status">Conectando...</div>
  <div id="batteries"></div>
  <p><a href="/download" download>Baixar Dados Completos</a></p>
  
  <script>
    const ws = new WebSocket('ws://' + location.hostname + '/ws');
    const statusDiv = document.getElementById('status');
    
    ws.onopen = () => statusDiv.textContent = "Conectado - Dados em Tempo Real";
    ws.onclose = () => statusDiv.textContent = "Conexão Perdida - Tentando reconectar...";
    
    ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      let html = '';
      
      data.voltages.forEach((v, i) => {
        html += `
        <div class="battery-container">
          <div class="battery ${data.alarm ? 'alarm' : ''}">
            <div class="level" style="height:${data.percentages[i]}%"></div>
          </div>
          <div class="info">
            Célula ${i+1}:<br>
            ${(v/1000).toFixed(2)}V<br>
            ${data.percentages[i]}%
          </div>
        </div>`;
      });
      
      document.getElementById('batteries').innerHTML = html;
      statusDiv.textContent = data.alarm 
        ? "ALARME: Sobretensão Detectada!" 
        : `Sistema OK - Total: ${(data.totalVoltage/1000).toFixed(2)}V (${data.totalPercentage}%)`;
      statusDiv.style.background = data.alarm ? "#ffebee" : "#e8f5e9";
    };
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  initHardware();
  initSPIFFS();
  initWiFi();
  initWebServer();
  
  Serial.println("\nSistema Iniciado");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
  static BatteryData data;
  static unsigned long lastRead = 0, lastSave = 0, lastReconnect = 0;
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastReconnect >= RECONNECT_INTERVAL) {
    WiFi.reconnect();
    lastReconnect = now;
  }

  if (now - lastRead >= READ_INTERVAL) {
    readBatteries(&data);
    calculatePercentages(&data);
    checkSafety(&data);
    updateLEDs(&data);
    sendWSData(&data);
    debugPrintData(&data);
    lastRead = now;
  }

  if (now - lastSave >= SAVE_INTERVAL) {
    saveData(&data);
    lastSave = now;
  }

  ws.cleanupClients();
  delay(10);
}

void initHardware() {
  // Configura LEDs
  for (int i = 0; i < NUM_BATTERIES; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  // Inicia I2C e ADS1115
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ads.begin(0x48)) {
    Serial.println("Falha ao iniciar ADS1115!");
    while(1);
  }
  ads.setGain(GAIN_ONE); // ±4.096V
}

void initWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFalha na conexão WiFi!");
  }
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Falha ao montar SPIFFS");
    return;
  }

  if (!SPIFFS.exists("/index.html")) {
    File file = SPIFFS.open("/index.html", FILE_WRITE);
    if (file) {
      file.print(index_html);
      file.close();
    }
  }

  if (!SPIFFS.exists("/data.csv")) {
    File file = SPIFFS.open("/data.csv", FILE_WRITE);
    if (file) {
      file.println("Timestamp,Volt1,Volt2,Volt3,Volt4,Percent1,Percent2,Percent3,Percent4,TotalVoltage,TotalPercent,Alarme");
      file.close();
    }
  }

  Serial.printf("SPIFFS: %d/%d bytes usados\n", SPIFFS.usedBytes(), SPIFFS.totalBytes());
}

void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (SPIFFS.exists("/data.csv")) {
      request->send(SPIFFS, "/data.csv", "text/csv", true);
    } else {
      request->send(404, "text/plain", "Arquivo não encontrado");
    }
  });

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  server.begin();
}

void readBatteries(BatteryData *data) {
  data->totalVoltage = 0;
  data->overVoltage = false;

  for (int i = 0; i < NUM_BATTERIES; i++) {
    int16_t adc = ads.readADC_SingleEnded(i);
    data->voltages[i] = adc * 0.125F; // 0.125mV por bit
    
    if (data->voltages[i] > OVER_VOLTAGE) {
      data->overVoltage = true;
    }
    
    data->totalVoltage += constrain(data->voltages[i], 0, OVER_VOLTAGE);
  }
}

void calculatePercentages(BatteryData *data) {
  data->totalPercentage = 0;
  
  for (int i = 0; i < NUM_BATTERIES; i++) {
    data->percentages[i] = constrain(
      map(data->voltages[i], MIN_VOLTAGE, MAX_VOLTAGE, 0, 100),
      0, 100
    );
    data->totalPercentage += data->percentages[i];
  }
  
  data->totalPercentage /= NUM_BATTERIES;
}

void checkSafety(BatteryData *data) {
  if (data->overVoltage) {
    Serial.println("ALERTA: Sobretensão detectada!");
    // Aqui você pode adicionar ações de proteção como desligar cargas
  }
}

void updateLEDs(const BatteryData *data) {
  for (int i = 0; i < NUM_BATTERIES; i++) {
    if (data->overVoltage) {
      digitalWrite(LED_PINS[i], (millis() % 200) < 100); // Pisca rápido
    } else if (data->percentages[i] >= 95) {
      digitalWrite(LED_PINS[i], HIGH); // Carga completa
    } else if (data->percentages[i] <= 5) {
      digitalWrite(LED_PINS[i], LOW); // Bateria vazia
    } else {
      // Pisca em frequência proporcional à carga
      int interval = map(data->percentages[i], 5, 95, 200, 2000);
      digitalWrite(LED_PINS[i], (millis() % interval) < (interval / 2));
    }
  }
}

void saveData(const BatteryData *data) {
  if (SPIFFS.usedBytes() > MAX_FILE_SIZE) {
    rotateFiles();
  }

  File file = SPIFFS.open("/data.csv", FILE_APPEND);
  if (!file) {
    Serial.println("Erro ao abrir arquivo!");
    return;
  }

  String line = String(millis());
  for (int i = 0; i < NUM_BATTERIES; i++) {
    line += "," + String(data->voltages[i]);
  }
  for (int i = 0; i < NUM_BATTERIES; i++) {
    line += "," + String(data->percentages[i]);
  }
  line += "," + String(data->totalVoltage);
  line += "," + String(data->totalPercentage);
  line += "," + String(data->overVoltage ? 1 : 0);

  if (!file.println(line)) {
    Serial.println("Falha na escrita!");
  }
  
  file.close();
}

void rotateFiles() {
  Serial.println("Rotacionando arquivos...");
  
  if (SPIFFS.exists("/data_old.csv")) {
    SPIFFS.remove("/data_old.csv");
  }
  
  if (SPIFFS.exists("/data.csv")) {
    SPIFFS.rename("/data.csv", "/data_old.csv");
  }
  
  File file = SPIFFS.open("/data.csv", FILE_WRITE);
  if (file) {
    file.println("Timestamp,Volt1,Volt2,Volt3,Volt4,Percent1,Percent2,Percent3,Percent4,TotalVoltage,TotalPercent,Alarme");
    file.close();
  }
}

void sendWSData(const BatteryData *data) {
  String json = "{";
  json += "\"voltages\":[" + String(data->voltages[0]);
  for (int i = 1; i < NUM_BATTERIES; i++) {
    json += "," + String(data->voltages[i]);
  }
  json += "],\"percentages\":[" + String(data->percentages[0]);
  for (int i = 1; i < NUM_BATTERIES; i++) {
    json += "," + String(data->percentages[i]);
  }
  json += "],\"totalVoltage\":" + String(data->totalVoltage);
  json += ",\"totalPercentage\":" + String(data->totalPercentage);
  json += ",\"alarm\":" + String(data->overVoltage ? "true" : "false");
  json += "}";
  
  ws.textAll(json);
}

void debugPrintData(const BatteryData *data) {
  Serial.println("\n--- Dados Atuais ---");
  for (int i = 0; i < NUM_BATTERIES; i++) {
    Serial.printf("Bateria %d: %.2fV (%d%%) %s\n", 
                 i+1, 
                 data->voltages[i]/1000.0, 
                 data->percentages[i],
                 (data->voltages[i] > OVER_VOLTAGE) ? "[ALERTA!]" : "");
  }
  Serial.printf("Total: %.2fV (%d%%)\n", data->totalVoltage/1000.0, data->totalPercentage);
  Serial.printf("Status: %s\n", data->overVoltage ? "ALARME - SOBRETENSÃO" : "Normal");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("-------------------");
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("Cliente #%u conectado\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("Cliente #%u desconectado\n", client->id());
  } else if (type == WS_EVT_ERROR) {
    Serial.printf("Erro WS: %u\n", *((uint16_t*)arg));
  }
}
/**
 * Verifica espaço disponível no SPIFFS
 */
void checkStorageSpace() {
  Serial.printf("Total space: %d bytes\n", SPIFFS.totalBytes());
  Serial.printf("Used space: %d bytes\n", SPIFFS.usedBytes());
  Serial.printf("Free space: %d bytes\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
}

/**
 * Lista todos arquivos no SPIFFS
 */
void listAllFiles() {
  Serial.println("Listing files in SPIFFS:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  
  while(file) {
    Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }
}

/**
 * Limpa todos dados armazenados (uso durante desenvolvimento)
 */
void formatSPIFFS() {
  Serial.println("Formatting SPIFFS...");
  SPIFFS.format();
  Serial.println("Done. SPIFFS formatted.");
}

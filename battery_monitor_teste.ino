/*
 * Projeto: Monitoramento Avançado de Baterias LiPo
 * ESP32-S3-DevKitC-1 + ADS1115
 * Funcionalidades:
 * - Leitura precisa de 4 células (16 bits)
 * - Cálculo de estado de carga
 * - Proteção contra sobretensão e subtensão
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

// =============== CONFIGURAÇÕES ===============
#define NUM_BATTERIES 4
const uint8_t LED_PINS[NUM_BATTERIES] = {12, 13, 14, 15};
const uint8_t I2C_SDA = 41;
const uint8_t I2C_SCL = 42;

// Parâmetros das Baterias LiPo (por célula)
const uint16_t MIN_VOLTAGE = 3000;    // 3.0V (0%) - NUNCA descarregar abaixo disso!
const uint16_t MAX_VOLTAGE = 4200;    // 4.2V (100%)
const uint16_t OVER_VOLTAGE = 4300;   // 4.3V (limite absoluto)
const uint16_t CRITICAL_LOW = 3200;   // 3.2V (nível crítico)
const size_t MAX_FILE_SIZE = 512000;  // 512KB (50% da partição)

// Configuração WiFi
const char* ssid = "Francisca";
const char* password = "Francisca2909";

// Intervalos de Operação (ms)
const uint32_t READ_INTERVAL = 5000;     // 5s = 5000
const uint32_t SAVE_INTERVAL = 300000;   // 5min
const uint32_t RECONNECT_INTERVAL = 30000;
const uint32_t SYSTEM_CHECK_INTERVAL = 3600000; // 1h

// =============== ESTRUTURAS DE DADOS ===============
#pragma pack(push, 1)
struct BatteryData {
  int16_t voltages[NUM_BATTERIES];
  int8_t percentages[NUM_BATTERIES];
  int16_t totalVoltage;
  int8_t totalPercentage;
  bool overVoltage;
  bool underVoltage;
  bool criticalLow;
};
#pragma pack(pop)

// =============== VARIÁVEIS GLOBAIS ===============
Adafruit_ADS1115 ads;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// =============== PROTÓTIPOS DE FUNÇÕES ===============
void initHardware();
bool initWiFi();
bool initSPIFFS();
void initWebServer();
bool readBatteries(BatteryData*);
void calculatePercentages(BatteryData*);
void handleSafety(BatteryData*);
void updateLEDs(const BatteryData*);
bool saveData(const BatteryData*);
void rotateFiles();
void sendWSData(const BatteryData*);
void onWebSocketEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t);
void logBatteryStatus(const BatteryData*);
void logSystemStatus();
void checkStorage();
void emergencyHandler();
void listFiles();

// =============== PÁGINA WEB ===============
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Monitor de Baterias LiPo</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 800px;
      margin: 0 auto;
      padding: 20px;
    }
    .header {
      text-align: center;
      margin-bottom: 20px;
    }
    .status {
      padding: 15px;
      margin-bottom: 20px;
      border-radius: 5px;
      font-weight: bold;
      text-align: center;
    }
    .normal {
      background-color: #e8f5e9;
      color: #2e7d32;
    }
    .warning {
      background-color: #fff8e1;
      color: #ff8f00;
      animation: pulse 2s infinite;
    }
    .alarm {
      background-color: #ffebee;
      color: #c62828;
      animation: pulse 1s infinite;
    }
    .battery-container {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
      gap: 20px;
      margin-bottom: 30px;
    }
    .battery-card {
      text-align: center;
      width: 120px;
    }
    .battery {
      width: 60px;
      height: 120px;
      border: 3px solid #333;
      position: relative;
      margin: 0 auto 10px;
    }
    .level {
      position: absolute;
      bottom: 0;
      width: 100%;
      background: #4CAF50;
      transition: height 0.5s;
    }
    .battery-info {
      font-size: 14px;
    }
    .download-btn {
      display: block;
      width: 200px;
      margin: 20px auto;
      padding: 10px;
      text-align: center;
      background: #2196F3;
      color: white;
      text-decoration: none;
      border-radius: 5px;
    }
    @keyframes pulse {
      0% { opacity: 1; }
      50% { opacity: 0.7; }
      100% { opacity: 1; }
    }
  </style>
</head>
<body>
  <div class="header">
    <h1>Monitor de Baterias LiPo</h1>
    <p>Sistema de Monitoramento em Tempo Real</p>
  </div>
  
  <div id="status" class="status normal">Conectando ao sistema...</div>
  
  <div id="batteries" class="battery-container">
    <!-- Dinamicamente preenchido via JavaScript -->
  </div>
  
  <a href="/download" class="download-btn">Baixar Dados Completos</a>
  
  <script>
    const ws = new WebSocket('ws://' + location.hostname + '/ws');
    const statusDiv = document.getElementById('status');
    
    ws.onopen = () => {
      statusDiv.textContent = "Sistema Conectado - Dados em Tempo Real";
      statusDiv.className = "status normal";
    };
    
    ws.onclose = () => {
      statusDiv.textContent = "Conexao Perdida - Tentando reconectar...";
      statusDiv.className = "status warning";
    };
    
    ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      let html = '';
      
      data.voltages.forEach((v, i) => {
        const voltage = (v/1000).toFixed(2);
        const percent = data.percentages[i];
        
        // Cor do nível da bateria baseado no estado
        let levelColor = '#4CAF50'; // Verde - normal
        if (v < data.criticalLow * 1000) levelColor = '#FF5722'; // Laranja - crítico
        if (v < data.minVoltage * 1000) levelColor = '#F44336'; // Vermelho - perigo
        
        html += `
        <div class="battery-card">
          <div class="battery">
            <div class="level" style="height:${percent}%; background:${levelColor}"></div>
          </div>
          <div class="battery-info">
            <strong>Celula ${i+1}</strong><br>
            ${voltage} V<br>
            ${percent}%
          </div>
        </div>`;
      });
      
      document.getElementById('batteries').innerHTML = html;
      
      if (data.alarm) {
        if (data.overVoltage) {
          statusDiv.textContent = "ALARME: Sobretensão Detectada!";
        } else if (data.underVoltage) {
          statusDiv.textContent = "ALARME: Subtensão Detectada!";
        }
        statusDiv.className = "status alarm";
      } else if (data.warning) {
        statusDiv.textContent = "AVISO: Nível crítico de carga!";
        statusDiv.className = "status warning";
      } else {
        statusDiv.textContent = `Sistema OK - Total: ${(data.totalVoltage/1000).toFixed(2)}V (${data.totalPercentage}%)`;
        statusDiv.className = "status normal";
      }
    };
    
    // Reconexão automática
    setInterval(() => {
      if (ws.readyState === WebSocket.CLOSED) {
        ws = new WebSocket('ws://' + location.hostname + '/ws');
      }
    }, 5000);
  </script>
</body>
</html>
)rawliteral";

// =============== SETUP INICIAL ===============
void setup() {
  Serial.begin(115200);
  delay(1000); // Estabilização
  
  initHardware();
  
  if (!initSPIFFS()) {
    emergencyHandler();
  }
  
  if (!initWiFi()) {
    Serial.println("[WIFI] Modo offline ativado - Sem conectividade");
  }
  
  initWebServer();
  
  Serial.println("\n[SISTEMA] Inicialização completa");
  Serial.println("=============================================");
  logSystemStatus();
}

// =============== LOOP PRINCIPAL ===============
void loop() {
  static BatteryData batData;
  static uint32_t lastRead = 0, lastSave = 0, lastReconnect = 0, lastCheck = 0;
  uint32_t currentMillis = millis();

  // Gerenciamento de Conexão WiFi
  if (WiFi.status() != WL_CONNECTED && currentMillis - lastReconnect >= RECONNECT_INTERVAL) {
    if (initWiFi()) {
      lastReconnect = currentMillis;
    }
  }

  // Leitura das Baterias
  if (currentMillis - lastRead >= READ_INTERVAL) {
    if (readBatteries(&batData)) {
      calculatePercentages(&batData);
      handleSafety(&batData);
      updateLEDs(&batData);
      sendWSData(&batData);
      logBatteryStatus(&batData);
    }
    lastRead = currentMillis;
  }

  // Armazenamento de Dados
  if (currentMillis - lastSave >= SAVE_INTERVAL) {
    if (saveData(&batData)) {
      lastSave = currentMillis;
    }
  }

  // Verificação Periódica do Sistema
  if (currentMillis - lastCheck >= SYSTEM_CHECK_INTERVAL) {
    checkStorage();
    logSystemStatus();
    lastCheck = currentMillis;
  }

  // Manutenção WebSocket
  ws.cleanupClients();
  
  delay(10); // Pausa mínima para estabilidade
}

// =============== IMPLEMENTAÇÃO DAS FUNÇÕES ===============

void initHardware() {
  // Configuração dos LEDs
  for (uint8_t i = 0; i < NUM_BATTERIES; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  // Inicialização do I2C e ADS1115
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ads.begin(0x48)) {
    Serial.println("[HARDWARE] Falha ao iniciar ADS1115!");
    emergencyHandler();
  }
  ads.setGain(GAIN_ONE); // Faixa de ±4.096V (ideal para LiPo)
  Serial.println("[HARDWARE] Periféricos inicializados com sucesso");
}

bool initWiFi() {
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("[WIFI] Conectando à rede ");
  Serial.print(ssid);
  
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] Falha na conexão!");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  
  Serial.println("\n[WIFI] Conectado com sucesso!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Falha na inicialização - Tentando formatar...");
    if (!SPIFFS.format() || !SPIFFS.begin(true)) {
      Serial.println("[SPIFFS] Falha crítica no sistema de arquivos!");
      return false;
    }
  }

  // Criar arquivos essenciais se não existirem
  if (!SPIFFS.exists("/index.html")) {
    File file = SPIFFS.open("/index.html", FILE_WRITE);
    if (file) {
      file.print(index_html);
      file.close();
      Serial.println("[SPIFFS] Arquivo HTML criado");
    } else {
      Serial.println("[SPIFFS] Falha ao criar arquivo HTML");
    }
  }

  if (!SPIFFS.exists("/data.csv")) {
    File file = SPIFFS.open("/data.csv", FILE_WRITE);
    if (file) {
      file.println("Timestamp,Volt1,Volt2,Volt3,Volt4,Percent1,Percent2,Percent3,Percent4,TotalVoltage,TotalPercent,Alarme");
      file.close();
      Serial.println("[SPIFFS] Arquivo CSV criado");
    } else {
      Serial.println("[SPIFFS] Falha ao criar arquivo CSV");
    }
  }

  Serial.printf("[SPIFFS] Inicializado - Espaço usado: %d/%d bytes\n", 
               SPIFFS.usedBytes(), SPIFFS.totalBytes());
  return true;
}

void initWebServer() {
  // Rota para a página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // Rota para download dos dados
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (SPIFFS.exists("/data.csv")) {
      request->send(SPIFFS, "/data.csv", "text/csv", true);
    } else {
      request->send(404, "text/plain", "Arquivo de dados não encontrado");
    }
  });

  // Configuração do WebSocket
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  // Inicia o servidor
  server.begin();
  Serial.println("[WEB] Servidor inicializado na porta 80");
}

bool readBatteries(BatteryData *data) {
  data->totalVoltage = 0;
  data->overVoltage = false;
  data->underVoltage = false;
  data->criticalLow = false;
  uint8_t errorCount = 0;

  for (uint8_t i = 0; i < NUM_BATTERIES; i++) {
    int16_t adc = ads.readADC_SingleEnded(i);
    
    // Verificação robusta de leitura
    if (adc == INT16_MAX || adc == INT16_MIN) {
      errorCount++;
      continue;
    }
    
    data->voltages[i] = adc * 0.125F; // Conversão para mV (0.125mV por bit)
    
    // Verificação de segurança
    if (data->voltages[i] > OVER_VOLTAGE) {
      data->overVoltage = true;
    } else if (data->voltages[i] < MIN_VOLTAGE) {
      data->underVoltage = true;
    } else if (data->voltages[i] < CRITICAL_LOW) {
      data->criticalLow = true;
    }
    
    data->totalVoltage += constrain(data->voltages[i], 0, OVER_VOLTAGE);
  }

  if (errorCount > 0) {
    Serial.printf("[ADC] %d leitura(s) inválida(s)\n", errorCount);
    return false;
  }
  return true;
}

void calculatePercentages(BatteryData *data) {
  uint16_t sum = 0;
  
  for (uint8_t i = 0; i < NUM_BATTERIES; i++) {
    // Curva de descarga não-linear aproximada para LiPo
    if (data->voltages[i] >= 4100) {
      data->percentages[i] = 90 + map(data->voltages[i], 4100, MAX_VOLTAGE, 0, 10);
    } else if (data->voltages[i] >= 3900) {
      data->percentages[i] = 70 + map(data->voltages[i], 3900, 4100, 0, 20);
    } else if (data->voltages[i] >= 3700) {
      data->percentages[i] = 30 + map(data->voltages[i], 3700, 3900, 0, 40);
    } else {
      data->percentages[i] = map(data->voltages[i], MIN_VOLTAGE, 3700, 0, 30);
    }
    
    data->percentages[i] = constrain(data->percentages[i], 0, 100);
    sum += data->percentages[i];
  }
  
  data->totalPercentage = sum / NUM_BATTERIES;
}

void handleSafety(BatteryData *data) {
  if (data->overVoltage) {
    Serial.println("[SEGURANÇA] ALERTA: Sobretensão detectada!");
    // Ações recomendadas:
    // - Desconectar carregador
    // - Ativar carga para drenagem
  } 
  else if (data->underVoltage) {
    Serial.println("[SEGURANÇA] ALERTA: Subtensão detectada!");
    // Ações recomendadas:
    // - Desligar todas as cargas
    // - Enviar alerta urgente
  }
  else if (data->criticalLow) {
    Serial.println("[SEGURANÇA] AVISO: Nível crítico de carga!");
  }
}

void updateLEDs(const BatteryData *data) {
  for (uint8_t i = 0; i < NUM_BATTERIES; i++) {
    if (data->overVoltage) {
      // Pisca rápido em caso de sobretensão (100ms)
      digitalWrite(LED_PINS[i], (millis() % 200) < 100);
    } 
    else if (data->underVoltage) {
      // Pisca lento em caso de subtensão (500ms)
      digitalWrite(LED_PINS[i], (millis() % 1000) < 500);
    }
    else if (data->criticalLow) {
      // Pisca alternado entre LEDs
      digitalWrite(LED_PINS[i], (millis() % 1000) < 500 && (i % 2 == (millis() / 500) % 2));
    }
    else if (data->percentages[i] >= 95) {
      digitalWrite(LED_PINS[i], HIGH); // Carga completa
    } 
    else if (data->percentages[i] <= 5) {
      digitalWrite(LED_PINS[i], LOW); // Bateria vazia
    } 
    else {
      // Pisca em frequência proporcional à carga (200ms a 2s)
      uint16_t interval = map(data->percentages[i], 5, 95, 200, 2000);
      digitalWrite(LED_PINS[i], (millis() % interval) < (interval / 2));
    }
  }
}

bool saveData(const BatteryData *data) {
  // Verificação de espaço disponível
  if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < 1024) { // Menos de 1KB livre
    Serial.println("[ARMAZENAMENTO] Espaço crítico - Rotacionando arquivos...");
    rotateFiles();
  }

  File file = SPIFFS.open("/data.csv", FILE_APPEND);
  if (!file) {
    Serial.println("[ARMAZENAMENTO] Falha ao abrir arquivo para escrita!");
    return false;
  }

  // Formato CSV otimizado
  String record = String(millis());
  for (uint8_t i = 0; i < NUM_BATTERIES; i++) {
    record += "," + String(data->voltages[i]);
    record += "," + String(data->percentages[i]);
  }
  record += "," + String(data->totalVoltage);
  record += "," + String(data->totalPercentage);
  record += "," + String(data->overVoltage ? "OVER" : (data->underVoltage ? "UNDER" : (data->criticalLow ? "LOW" : "OK")));

  if (!file.println(record)) {
    Serial.println("[ARMAZENAMENTO] Falha na escrita dos dados!");
    file.close();
    return false;
  }
  
  file.close();
  return true;
}

void rotateFiles() {
  // Remove arquivo antigo se existir
  if (SPIFFS.exists("/data_old.csv")) {
    SPIFFS.remove("/data_old.csv");
  }
  
  // Renomeia arquivo atual para backup
  if (SPIFFS.exists("/data.csv")) {
    SPIFFS.rename("/data.csv", "/data_old.csv");
  }
  
  // Cria novo arquivo com cabeçalho
  File file = SPIFFS.open("/data.csv", FILE_WRITE);
  if (file) {
    file.println("Timestamp,Volt1,Volt2,Volt3,Volt4,Percent1,Percent2,Percent3,Percent4,TotalVoltage,TotalPercent,Status");
    file.close();
    Serial.println("[ARMAZENAMENTO] Rotação de arquivos concluída");
  }
}

void sendWSData(const BatteryData *data) {
  // Prepara JSON com os dados
  String json = "{";
  json += "\"voltages\":[" + String(data->voltages[0]);
  for (uint8_t i = 1; i < NUM_BATTERIES; i++) {
    json += "," + String(data->voltages[i]);
  }
  json += "],\"percentages\":[" + String(data->percentages[0]);
  for (uint8_t i = 1; i < NUM_BATTERIES; i++) {
    json += "," + String(data->percentages[i]);
  }
  json += "],\"totalVoltage\":" + String(data->totalVoltage);
  json += ",\"totalPercentage\":" + String(data->totalPercentage);
  json += ",\"minVoltage\":" + String(MIN_VOLTAGE/1000.0, 2);
  json += ",\"criticalLow\":" + String(CRITICAL_LOW/1000.0, 2);
  json += ",\"overVoltage\":" + String(data->overVoltage ? "true" : "false");
  json += ",\"underVoltage\":" + String(data->underVoltage ? "true" : "false");
  json += ",\"alarm\":" + String(data->overVoltage || data->underVoltage ? "true" : "false");
  json += ",\"warning\":" + String(data->criticalLow ? "true" : "false");
  json += "}";
  
  // Envia para todos clientes conectados
  ws.textAll(json);
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WEB] Cliente #%u conectado\n", client->id());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WEB] Cliente #%u desconectado\n", client->id());
      break;
    case WS_EVT_ERROR:
      Serial.printf("[WEB] Erro no cliente #%u: %u\n", client->id(), *((uint16_t*)arg));
      break;
    case WS_EVT_DATA:
      // Pode processar mensagens recebidas aqui se necessário
      break;
  }
}

void logBatteryStatus(const BatteryData *data) {
  static uint32_t counter = 0;
  Serial.printf("\n[LEITURA #%u] Tempo: %.1f horas\n", ++counter, millis()/3600000.0);
  
  for (uint8_t i = 0; i < NUM_BATTERIES; i++) {
    Serial.printf("Bateria %u: %4dmV (%3d%%) %s\n", 
                 i+1, 
                 data->voltages[i], 
                 data->percentages[i],
                 (data->voltages[i] > OVER_VOLTAGE) ? "[SOBRETENSÃO]" : 
                 (data->voltages[i] < MIN_VOLTAGE) ? "[SUBTENSÃO]" :
                 (data->voltages[i] < CRITICAL_LOW) ? "[CRÍTICO]" : "");
  }
  
  Serial.printf("Total: %5dmV (%3d%%)\n", data->totalVoltage, data->totalPercentage);
  
  if (data->overVoltage) {
    Serial.println("Status: ALARME - SOBRETENSÃO");
  } else if (data->underVoltage) {
    Serial.println("Status: ALARME - SUBTENSÃO");
  } else if (data->criticalLow) {
    Serial.println("Status: AVISO - NÍVEL CRÍTICO");
  } else {
    Serial.println("Status: Normal");
  }
}

void logSystemStatus() {
  Serial.println("\n=== STATUS DO SISTEMA ===");
  Serial.printf("Tempo de atividade: %.1f horas\n", millis()/3600000.0);
  Serial.printf("Memória livre: %6d bytes\n", ESP.getFreeHeap());
  Serial.printf("SPIFFS livre: %6d bytes\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
  Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? 
               WiFi.localIP().toString().c_str() : "Desconectado");
  Serial.printf("Clientes WebSocket: %d\n", ws.count());
  Serial.println("=========================");
}

void checkStorage() {
  size_t used = SPIFFS.usedBytes();
  size_t total = SPIFFS.totalBytes();
  
  Serial.printf("[ARMAZENAMENTO] Uso: %d/%d bytes (%.1f%%)\n", 
               used, total, (used * 100.0) / total);
  
  if (total - used < 10240) { // Menos de 10KB livres
    Serial.println("[ARMAZENAMENTO] Espaço crítico!");
  }
}

void emergencyHandler() {
  Serial.println("\n[EMERGÊNCIA] Sistema em modo de recuperação");
  
  // Piscar todos os LEDs rapidamente para indicar erro
  while (true) {
    for (uint8_t i = 0; i < NUM_BATTERIES; i++) {
      digitalWrite(LED_PINS[i], !digitalRead(LED_PINS[i]));
    }
    delay(200);
  }
}

void listFiles() {
  Serial.println("\n[SPIFFS] Lista de arquivos:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  
  while(file) {
    Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }
}
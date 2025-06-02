/*
 * Projeto: Monitoramento de Baterias com ESP32-S3, ADS1115 e SPIFFS
 * Funcionalidades:
 * - Leitura de 4 células de bateria via ADS1115 (I2C)
 * - Cálculo do estado de carga
 * - Armazenamento local em SPIFFS
 * - Interface Web com atualização em tempo real via WebSocket
 * - Indicadores visuais via LED
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_ADS1X15.h>
#include <SPIFFS.h>  // Alterado para SPIFFS

// Altere o tamanho dos arrays e loops for
#define NUM_BATTERIES 4

// Definições de bateria
const int16_t MIN_BATTERY_VOLTAGE = 3000;  // 3000mV = 0%
const int16_t MAX_BATTERY_VOLTAGE = 4200;  // 4200mV = 100%
const size_t MAX_FILE_SIZE = 512000;       // 512KB (metade da partição típica)

// Definições dos pinos
const int LED_PINS[] = {12, 13, 14, 15};
const int I2C_SDA = 17;
const int I2C_SCL = 18;

// Configurações WiFi
const char* ssid = "SUA_REDE_WIFI";
const char* password = "SUA_SENHA";

// Intervalos de tempo
const unsigned long READ_INTERVAL = 1000; // 1 segundo
const unsigned long SAVE_INTERVAL = 60000; // 1 minuto
const unsigned long RECONNECT_INTERVAL = 30000;

// Objetos globais
Adafruit_ADS1115 ads;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Estrutura para dados da bateria
struct BatteryData {
  int16_t voltages[NUM_BATTERIES];
  int8_t percentages[NUM_BATTERIES];
  int16_t totalVoltage;
  int8_t totalPercentage;
};

// Variáveis globais
unsigned long lastReadTime = 0;
unsigned long lastSaveTime = 0;
unsigned long lastReconnectAttempt = 0;

// Protótipos de funções
void initWiFi();
void initWebServer();
void initStorage();
void onWebSocketEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t);
void readBatteryData(BatteryData*);
void calculatePercentages(BatteryData*);
void updateLEDs(const BatteryData*);
void saveDataToFile(const BatteryData*);
void rotateDataFiles();
void sendDataToClients(const BatteryData*);

// Página HTML (idêntica à versão anterior)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Monitor de Baterias</title>
  <style>
    .battery { width: 100px; height: 200px; border: 3px solid #000; position: relative; }
    .level { position: absolute; bottom: 0; width: 100%; background: #4CAF50; }
  </style>
</head>
<body>
  <h1>Monitor de Baterias</h1>
  <div id="batteries"></div>
  <script>
    const ws = new WebSocket('ws://' + location.hostname + '/ws');
    ws.onmessage = function(event) {
      const data = JSON.parse(event.data);
      let html = '';
      data.voltages.forEach((v, i) => {
        html += `<div><div class="battery"><div class="level" style="height:${data.percentages[i]}%"></div></div>
                <p>Bateria ${i+1}: ${v}mV (${data.percentages[i]}%)</p></div>`;
      });
      document.getElementById('batteries').innerHTML = html;
    };
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  
  // Inicializa LEDs
  for (int i = 0; i < 4; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  // Inicializa I2C e ADS1115
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ads.begin(0x48)) {
    Serial.println("Falha ao iniciar ADS1115!");
    while (1);
  }
  ads.setGain(GAIN_ONE);

  // Inicializa armazenamento com SPIFFS
  initStorage();

  // Conecta WiFi
  initWiFi();

  // Inicializa servidor web
  initWebServer();
  
  Serial.println("\nInformações do SPIFFS:");
  checkStorageSpace();
  listAllFiles();
  
  // Descomente apenas se precisar formatar
  // formatSPIFFS();
}

void loop() {
  static BatteryData currentData;
  unsigned long currentMillis = millis();

  // Reconecta WiFi se necessário
  if (WiFi.status() != WL_CONNECTED && currentMillis - lastReconnectAttempt >= RECONNECT_INTERVAL) {
    Serial.println("Reconectando WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
    lastReconnectAttempt = currentMillis;
  }

  // Leitura periódica das baterias
  if (currentMillis - lastReadTime >= READ_INTERVAL) {
    readBatteryData(&currentData);
    calculatePercentages(&currentData);
    updateLEDs(&currentData);
    sendDataToClients(&currentData);
    lastReadTime = currentMillis;
  }

  // Armazenamento periódico
  if (currentMillis - lastSaveTime >= SAVE_INTERVAL) {
    saveDataToFile(&currentData);
    lastSaveTime = currentMillis;
  }

  // Limpa clientes desconectados do WebSocket
  ws.cleanupClients();

  // Verificação periódica a cada hora
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 3600000) {
    checkStorageSpace();
    lastCheck = millis();
  }
}

/**
 * Inicializa o SPIFFS
 */
void initStorage() {
  if (!SPIFFS.begin(true)) {  // true = formata se montagem falhar
    Serial.println("Erro ao montar SPIFFS");
    return;
  }
  
  // Verifica se o arquivo existe, se não, cria com cabeçalho
  if (!SPIFFS.exists("/battery_data.csv")) {
    File file = SPIFFS.open("/battery_data.csv", FILE_WRITE);
    if (file) {
      file.println("Timestamp,Volt1,Volt2,Volt3,Volt4,Percent1,Percent2,Percent3,Percent4,TotalVoltage,TotalPercent");
      file.close();
      Serial.println("Arquivo CSV criado com cabeçalho");
    } else {
      Serial.println("Erro ao criar arquivo");
    }
  }
  Serial.println("SPIFFS inicializado");
}

/**
 * Salva dados no arquivo CSV usando SPIFFS
 */
void saveDataToFile(const BatteryData *data) {
  // Verifica espaço disponível
  if (SPIFFS.usedBytes() > MAX_FILE_SIZE) {
    rotateDataFiles();
  }

  File file = SPIFFS.open("/battery_data.csv", FILE_APPEND);
  if (!file) {
    Serial.println("Erro ao abrir arquivo para escrita");
    return;
  }

  // Formato mais robusto com verificação de escrita
  size_t bytesWritten = file.print(millis());
  for (int i = 0; i < NUM_BATTERIES; i++) {
    bytesWritten += file.print(",");
    bytesWritten += file.print(data->voltages[i]);
  }
  for (int i = 0; i < NUM_BATTERIES; i++) {
    bytesWritten += file.print(",");
    bytesWritten += file.print(data->percentages[i]);
  }
  bytesWritten += file.print(",");
  bytesWritten += file.print(data->totalVoltage);
  bytesWritten += file.print(",");
  bytesWritten += file.println(data->totalPercentage);

  if (bytesWritten == 0) {
    Serial.println("Falha ao escrever no arquivo!");
  } else {
    Serial.printf("Dados salvos (%d bytes)\n", bytesWritten);
  }
  
  file.close();
}

// Adicione esta nova função para rotacionar arquivos
void rotateDataFiles() {
  Serial.println("Rotacionando arquivos de dados...");
  
  if (SPIFFS.exists("/battery_data_old.csv")) {
    SPIFFS.remove("/battery_data_old.csv");
  }
  
  if (SPIFFS.exists("/battery_data.csv")) {
    SPIFFS.rename("/battery_data.csv", "/battery_data_old.csv");
  }
  
  // Cria novo arquivo
  File file = SPIFFS.open("/battery_data.csv", FILE_WRITE);
  if (file) {
    file.println("Timestamp,Volt1,Volt2,Volt3,Volt4,Percent1,Percent2,Percent3,Percent4,TotalVoltage,TotalPercent");
    file.close();
  }
}

/**
 * Inicializa servidor web com rota para download
 */
void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  // Rota para download dos dados
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (SPIFFS.exists("/battery_data.csv")) {
      request->send(SPIFFS, "/battery_data.csv", "text/csv");
    } else {
      request->send(404, "text/plain", "Arquivo não encontrado");
    }
  });

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  server.begin();
}

/**
 * Inicializa a conexão WiFi
 */
void initWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
}

/**
 * Lê os dados das baterias do ADS1115
 * @param data Ponteiro para estrutura BatteryData
 */
void readBatteryData(BatteryData *data) {
  data->totalVoltage = 0;
  
  for (int i = 0; i < 4; i++) {
    // Lê valor do ADC (16 bits)
    int16_t adc = ads.readADC_SingleEnded(i);
    
    // Converte para mV (GAIN_ONE: 0.125mV por bit)
    data->voltages[i] = adc * 0.125F;
    data->totalVoltage += data->voltages[i];
    
    // Limita valores negativos (pode ocorrer com ruído)
    if (data->voltages[i] < 0) data->voltages[i] = 0;
  }
}

/**
 * Calcula as porcentagens de carga baseado nas tensões
 * @param data Ponteiro para estrutura BatteryData
 */
void calculatePercentages(BatteryData *data) {
  data->totalPercentage = 0;
  
  for (int i = 0; i < NUM_BATTERIES; i++) {
    data->percentages[i] = map(data->voltages[i], 
                              MIN_BATTERY_VOLTAGE, 
                              MAX_BATTERY_VOLTAGE, 
                              0, 100);
    data->percentages[i] = constrain(data->percentages[i], 0, 100);
    data->totalPercentage += data->percentages[i];
  }
  data->totalPercentage /= NUM_BATTERIES;
}
  
  // Calcula média
  data->totalPercentage /= 4;
}

/**
 * Atualiza os LEDs conforme estado das baterias
 * @param data Ponteiro para estrutura BatteryData
 */
void updateLEDs(const BatteryData *data) {
  for (int i = 0; i < 4; i++) {
    if (data->percentages[i] >= 90) {
      digitalWrite(LED_PINS[i], HIGH);  // Cheia - LED aceso
    } else if (data->percentages[i] <= 10) {
      digitalWrite(LED_PINS[i], LOW);   // Vazia - LED apagado
    } else {
      // Pisca em frequência proporcional à carga
      int interval = map(data->percentages[i], 10, 90, 200, 1000);
      digitalWrite(LED_PINS[i], (millis() % interval) < (interval / 2));
    }
  }
}

/**
 * Envia dados para clientes via WebSocket
 * @param data Ponteiro para estrutura BatteryData
 */
void sendDataToClients(const BatteryData *data) {
  // Prepara JSON com os dados
  String json = "{";
  json += "\"voltages\":[" + String(data->voltages[0]) + "," + String(data->voltages[1]) + "," + 
          String(data->voltages[2]) + "," + String(data->voltages[3]) + "],";
  json += "\"percentages\":[" + String(data->percentages[0]) + "," + String(data->percentages[1]) + "," + 
          String(data->percentages[2]) + "," + String(data->percentages[3]) + "],";
  json += "\"totalVoltage\":" + String(data->totalVoltage) + ",";
  json += "\"totalPercentage\":" + String(data->totalPercentage);
  json += "}";
  
  // Envia para todos clientes conectados
  ws.textAll(json);
}

/**
 * Manipulador de eventos do WebSocket
 */
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("Cliente WebSocket #%u conectado\n", client->id());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("Cliente WebSocket #%u desconectado\n", client->id());
      break;
    case WS_EVT_DATA:
      // Pode tratar mensagens recebidas aqui
      break;
    case WS_EVT_ERROR:
      // Trata erros
      break;
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

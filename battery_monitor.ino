/*
 * Sistema de Monitoramento de Bateria com ESP32-S3 e ADS1115
 * 
 * Este código implementa um sistema de monitoramento de células de bateria de lítio
 * utilizando ESP32-S3 DevKitC1 e ADS1115 como conversor analógico-digital externo.
 * 
 * Configuração de Hardware:
 * - ESP32-S3 DevKitC1
 *   - GPIO42: SDA (I2C)
 *   - GPIO41: SCL (I2C)
 * 
 * - ADS1115
 *   - Resolução: 16 bits
 *   - Ganho: GAIN_ONE (±4.096V)
 *   - Endereço I2C: 0x48
 *   - Alimentação: 3.3V
 * 
 * - Divisores Resistivos:
 *   - R1 = [2171, 4696, 6820, 6790] Ω
 *   - R2 = [4689, 2168, 2172, 984] Ω
 * 
 * Bibliotecas utilizadas:
 * - Wire.h: interface I²C
 * - Adafruit_ADS1X15.h: driver do ADS1115
 * - WiFi.h: gerenciamento da conexão Wi-Fi
 * - time.h: sincronização via NTP
 * - AsyncTCP.h e ESPAsyncWebServer.h: WebSocket e servidor HTTP assíncrono
 */

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <WiFi.h>
#include <time.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Configurações de Wi-Fi
const char* ssid = "SUA_REDE_WIFI";
const char* password = "SUA_SENHA_WIFI";

// Configurações do servidor NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -10800;  // Fuso horário Brasil (GMT-3): -3 * 60 * 60
const int daylightOffset_sec = 0;   // Sem horário de verão

// Configurações do ADS1115
Adafruit_ADS1115 ads;  // Instância do ADS1115 (endereço padrão 0x48)
const float ADS_LSB = 0.125F;  // LSB = 4.096V / 32768 ≈ 0.125mV para GAIN_ONE

// Configurações dos divisores resistivos
const float R1[4] = {2171.0, 4696.0, 6820.0, 6790.0};  // Resistores superiores (ohms)
const float R2[4] = {4689.0, 2168.0, 2172.0, 984.0};   // Resistores inferiores (ohms)

// Fator de divisão para cada canal
float dividerFactor[4];

// Servidor Web Assíncrono na porta 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Variáveis para armazenar as leituras
float cellVoltage[4];
unsigned long lastReadTime = 0;
const unsigned long readInterval = 1000;  // Intervalo de leitura em ms

// HTML da página web (armazenado na memória flash)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>Monitoramento de Bateria</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h2 {font-size: 2.4rem;}
    p {font-size: 1.6rem;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    .cell {margin-bottom: 20px; padding: 10px; border-radius: 5px;}
    .cell-1 {background-color: #f8f8f8;}
    .cell-2 {background-color: #f0f0f0;}
    .cell-3 {background-color: #e8e8e8;}
    .cell-4 {background-color: #e0e0e0;}
    .voltage {font-size: 2.2rem; font-weight: bold;}
    .time {font-size: 1rem; color: #888;}
  </style>
</head>
<body>
  <h2>Monitoramento de Células de Bateria</h2>
  
  <div class="cell cell-1">
    <p>Célula 1</p>
    <p class="voltage" id="voltage1">--</p>
    <p class="time" id="time">--</p>
  </div>
  
  <div class="cell cell-2">
    <p>Célula 2</p>
    <p class="voltage" id="voltage2">--</p>
  </div>
  
  <div class="cell cell-3">
    <p>Célula 3</p>
    <p class="voltage" id="voltage3">--</p>
  </div>
  
  <div class="cell cell-4">
    <p>Célula 4</p>
    <p class="voltage" id="voltage4">--</p>
  </div>

<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  
  window.addEventListener('load', onLoad);
  
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
  }
  
  function onOpen(event) {
    console.log('Connection opened');
  }
  
  function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
  }
  
  function onMessage(event) {
    var data = JSON.parse(event.data);
    document.getElementById('voltage1').innerHTML = data.v1.toFixed(3) + ' V';
    document.getElementById('voltage2').innerHTML = data.v2.toFixed(3) + ' V';
    document.getElementById('voltage3').innerHTML = data.v3.toFixed(3) + ' V';
    document.getElementById('voltage4').innerHTML = data.v4.toFixed(3) + ' V';
    document.getElementById('time').innerHTML = data.time;
  }
  
  function onLoad(event) {
    initWebSocket();
  }
</script>
</body>
</html>
)rawliteral";

// Função para calcular a tensão real considerando os divisores resistivos
float calculateVoltage(float adcVoltage, int channel) {
  return adcVoltage * dividerFactor[channel];
}

// Função para obter a hora atual formatada
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Falha ao obter hora";
  }
  char timeString[30];
  strftime(timeString, sizeof(timeString), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(timeString);
}

// Função para enviar dados via WebSocket
void sendSensorData() {
  String json = "{";
  json += "\"v1\":" + String(cellVoltage[0], 3) + ",";
  json += "\"v2\":" + String(cellVoltage[1], 3) + ",";
  json += "\"v3\":" + String(cellVoltage[2], 3) + ",";
  json += "\"v4\":" + String(cellVoltage[3], 3) + ",";
  json += "\"time\":\"" + getFormattedTime() + "\"";
  json += "}";
  ws.textAll(json);
}

// Manipulador de eventos WebSocket
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      // Não processamos dados recebidos neste exemplo
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void setup() {
  // Inicializa a comunicação serial
  Serial.begin(115200);
  Serial.println("Sistema de Monitoramento de Bateria com ESP32-S3 e ADS1115");
  
  // Calcula os fatores de divisão para cada canal
  for (int i = 0; i < 4; i++) {
    dividerFactor[i] = (R1[i] + R2[i]) / R2[i];
    Serial.printf("Canal %d - Fator de divisão: %.4f\n", i, dividerFactor[i]);
  }
  
  // Inicializa a comunicação I2C com os pinos específicos do ESP32-S3
  Wire.begin(42, 41);  // SDA = GPIO42, SCL = GPIO41
  
  // Inicializa o ADS1115
  if (!ads.begin()) {
    Serial.println("Falha ao inicializar o ADS1115!");
    while (1);
  }
  
  // Configura o ganho do ADS1115 para GAIN_ONE (±4.096V)
  ads.setGain(GAIN_ONE);
  Serial.println("ADS1115 inicializado com sucesso!");
  Serial.println("Ganho configurado para GAIN_ONE (±4.096V)");
  Serial.printf("Valor do LSB: %.6f mV\n", ADS_LSB);
  
  // Conecta ao Wi-Fi
  Serial.printf("Conectando à rede Wi-Fi %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONECTADO!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
  
  // Configura o servidor NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Configura o manipulador de eventos WebSocket
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  
  // Rota para a página web
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  
  // Inicia o servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  // Limpa conexões WebSocket inativas
  ws.cleanupClients();
  
  // Lê os valores do ADS1115 a cada intervalo definido
  unsigned long currentTime = millis();
  if (currentTime - lastReadTime >= readInterval) {
    lastReadTime = currentTime;
    
    // Lê os valores dos 4 canais do ADS1115
    int16_t adc0, adc1, adc2, adc3;
    
    adc0 = ads.readADC_SingleEnded(0);
    adc1 = ads.readADC_SingleEnded(1);
    adc2 = ads.readADC_SingleEnded(2);
    adc3 = ads.readADC_SingleEnded(3);
    
    // Converte para tensão (V)
    float voltage0 = ads.computeVolts(adc0);
    float voltage1 = ads.computeVolts(adc1);
    float voltage2 = ads.computeVolts(adc2);
    float voltage3 = ads.computeVolts(adc3);
    
    // Calcula a tensão real considerando os divisores resistivos
    cellVoltage[0] = calculateVoltage(voltage0, 0);
    cellVoltage[1] = calculateVoltage(voltage1, 1);
    cellVoltage[2] = calculateVoltage(voltage2, 2);
    cellVoltage[3] = calculateVoltage(voltage3, 3);
    
    // Exibe os valores no console serial
    Serial.println("----------------------------------------");
    Serial.printf("Canal 0: ADC = %d, Tensão = %.3f V, Tensão Real = %.3f V\n", 
                  adc0, voltage0, cellVoltage[0]);
    Serial.printf("Canal 1: ADC = %d, Tensão = %.3f V, Tensão Real = %.3f V\n", 
                  adc1, voltage1, cellVoltage[1]);
    Serial.printf("Canal 2: ADC = %d, Tensão = %.3f V, Tensão Real = %.3f V\n", 
                  adc2, voltage2, cellVoltage[2]);
    Serial.printf("Canal 3: ADC = %d, Tensão = %.3f V, Tensão Real = %.3f V\n", 
                  adc3, voltage3, cellVoltage[3]);
    Serial.println("----------------------------------------");
    
    // Envia os dados via WebSocket
    sendSensorData();
  }
}

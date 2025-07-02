# 🔋 Monitoramento de Bateria LiPo 4S com ESP32

Este projeto implementa um sistema completo de monitoramento, calibração, registro e visualização de dados de um pack de bateria LiPo 4 células (4S) usando ESP32. O sistema possui interface web moderna, registro em CSV, calibração fácil e comunicação em tempo real via WebSocket.

## ⚡ Principais Funcionalidades

- **Amostragem em tempo real:** Leitura das 4 células a cada 500ms (2Hz) com oversampling e validação.
- **Calibração via Web:** Interface para ajuste dos fatores de divisão (kDiv) diretamente pelo navegador.
- **Registro de dados:** Todos os dados são salvos em CSV na SPIFFS, com rotação automática de arquivo.
- **Dashboard Web:** Visualização ao vivo dos dados, gráficos e download dos logs.
- **API REST e WebSocket:** Comunicação eficiente para monitoramento e integração.
- **Robustez:** Detecção de falhas, reinício automático e fallback para valores padrão.

## 🗂️ Estrutura dos Arquivos

- `main.ino` — Inicialização, loop principal, controle de fluxo e integração dos módulos.
- `ads_driver.h/cpp` — Driver do ADC (ADS1115): oversampling, leitura, calibração e validação dos dados.
- `config.h/cpp` — Gerenciamento dos fatores de calibração (kDiv) via arquivo `/config.json` na SPIFFS.
- `storage.h/cpp` — Registro dos dados em CSV, rotação e limpeza dos logs.
- `net.h/cpp` — Inicialização WiFi, servidor HTTP/WS, API REST, dashboard web e endpoints de calibração/download.
- `partitions.csv` — Tabela de partições para SPIFFS e OTA.

## 🌐 Interface Web

- **Monitor:** Visualização ao vivo das tensões das células, total e gráfico das últimas 60 amostras.
- **Setup:** Calibração dos canais (kDiv) e limpeza dos logs.
- **Download:** Baixe o log completo em CSV.

## 🔗 Endpoints e APIs

- `/` — Dashboard web (HTML/JS/CSS embarcado)
- `/ws` — WebSocket para atualização em tempo real
- `/download` — Download do log CSV
- `/api/calibrate` — POST para calibração (JSON)
- `/api/clear_logs` — POST para limpar logs
- `/api/raw` — Consulta dos valores brutos do ADC (JSON)

## 🚀 Como Usar

1. **Hardware:**
   - ESP32 (ex: ESP32S3)
   - ADS1115 (ADC externo)
   - Pack LiPo 4S
2. **Compilação:**
   - Use PlatformIO ou Arduino IDE
   - Instale as bibliotecas: `ESPAsyncWebServer`, `AsyncTCP`, `ArduinoJson`, `Adafruit_ADS1X15`, etc.
3. **Configuração:**
   - Defina SSID e senha WiFi em `net.cpp`.
   - Faça upload do firmware.
4. **Acesso:**
   - Conecte o ESP32 à rede WiFi.
   - Acesse `http://<ip-do-esp32>/` no navegador.
5. **Calibração:**
   - Meça as tensões reais das células com multímetro.
   - Insira os valores na aba Setup e aplique.
6. **Monitoramento e Download:**
   - Visualize os dados em tempo real e baixe o log CSV pela interface web.

## 🛠️ Arquitetura dos Módulos

- **ADC/Calibração:**
  - Oversampling, validação e ajuste dos fatores kDiv para cada canal.
- **Configuração:**
  - Leitura e gravação dos fatores de calibração em `/config.json`.
- **Armazenamento:**
  - Log em CSV com rotação automática e limpeza via API.
- **Rede/Web:**
  - Servidor HTTP/WS, dashboard embarcado, endpoints REST e WebSocket.

## 📋 Observações

- O sistema entra em deep sleep por 5 minutos se não conectar ao WiFi.
- Logs e calibração persistem na SPIFFS.
- Reinício automático em caso de falhas críticas no ADC.

## 👨‍💻 Autor
Alyson Gonçalo
André Marques
Nonato Silva
---

> Projeto TFG — Engenharia de Computação UNIFEI

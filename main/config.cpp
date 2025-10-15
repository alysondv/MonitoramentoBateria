#include "config.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

/**
 * Tenta carregar os fatores de calibração kDiv a partir do /config.json.
 * Se o arquivo não existir ou estiver corrompido, a função retorna false
 * e o sistema deve usar os valores de calibração padrão.
 */
bool CFG_load(Calib &c) {
    File f = SPIFFS.open("/config.json");
    // Arquivo de config não encontrado, usa os valores default. Normal na primeira execução.
    if (!f) {
        return false;
    }
    
    // Se o arquivo existe, mas está vazio, trate-o como se não existisse.
    if (f.size() == 0) {
        Serial.println(F("[CFG] Arquivo config.json encontrado, mas está vazio. Usando defaults."));
        f.close();
        return false;
    }
    
    // Usar 256 bytes para o doc JSON, pra ter uma folga. 128 pode ser pouco no futuro.
    StaticJsonDocument<256> doc;
    
    // Tenta fazer o parse do JSON.
    DeserializationError error = deserializeJson(doc, f);
    f.close(); // Boa prática: fecha o arquivo assim que terminar a leitura.

    if (error) {
        Serial.print(F("[CFG] Falha no parse do config.json, usando defaults. Erro: "));
        Serial.println(error.c_str());
        return false;
    }

    // O JSON deve ter um array chamado "k".
    JsonArray k_array = doc["k"];
    if (k_array.isNull()) {
        // Chave "k" não encontrada no JSON, arquivo inválido.
        return false;
    }

    // Lê os 4 valores do array e preenche a struct Calib.
    for (int i = 0; i < 4; i++) {
        JsonVariant k_val = k_array[i];
        
        // Se um dos valores no array for nulo, não sobrescreve o default.
        if (!k_val.isNull()) {
            c.kDiv[i] = k_val.as<float>();
        }
        // Se for nulo, o valor padrão que já está em 'c' será mantido.
    }
    
    return true;
}

/**
 * Salva a estrutura de calibração 'c' no arquivo /config.json na SPIFFS.
 * O arquivo existente será sobrescrito.
 */
void CFG_save(const Calib &c) {
    StaticJsonDocument<128> d;
    // Cria o array "k" no objeto JSON.
    JsonArray k_array = d.createNestedArray("k");
    for (int i = 0; i < 4; i++) {
        k_array.add(c.kDiv[i]);
    }
    
    File f = SPIFFS.open("/config.json", "w");
    if (!f) {
        Serial.println(F("[CFG] Erro ao abrir config.json para escrita"));
        return;
    }

    // Escreve o JSON formatado no arquivo.
    serializeJson(d, f);
    f.close();
}
#pragma once

/**
 * Estrutura de calibração dos divisores de tensão.
 * kDiv: ganhos por canal (multiplicativo)
 * oMv : offsets por canal em mV (aditivo, aplicado no cumulativo)
 */
struct Calib {
    float kDiv[4];
    float oMv[4];
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
#pragma once
#include "ads_driver.h"

/**
 * Inicializa o sistema de arquivos.
 * @return true se bem sucedido, false em caso de erro.
 */
bool FS_init();

/**
 * Adiciona uma amostra ao arquivo CSV.
 * @param s Amostra a ser salva.
 * @return true se bem sucedido, false em caso de erro.
 */
bool FS_appendCsv(const CellSample &s);

/**
 * Limpa todos os logs.
 * @return true se bem sucedido, false em caso de erro.
 */
bool FS_clearLogs();
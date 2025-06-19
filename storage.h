#pragma once
#include "ads_driver.h"

// Inicializa o sistema de arquivos
// Retorna true se bem sucedido, false em caso de erro
bool FS_init();

// Adiciona uma amostra ao arquivo CSV
// Retorna true se bem sucedido, false em caso de erro
bool FS_appendCsv(const CellSample &s);

// Limpa todos os logs
// Retorna true se bem sucedido, false em caso de erro
bool FS_clearLogs();
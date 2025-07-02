#pragma once
#include "ads_driver.h"

/**
 * Inicializa WiFi, servidor HTTP/WS e endpoints.
 */
void NET_init();

/**
 * Envia uma amostra via WebSocket.
 * @param s Amostra a ser enviada.
 */
void NET_tick(const CellSample &s);
#pragma once

#include "esp_eth.h"
#include "esp_err.h"

void test_ip101_connection(void);
esp_eth_handle_t eth_bringup_get_handle(void);
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t board_i2c_master_bus_create(i2c_master_bus_handle_t *out_bus);
esp_err_t board_i2c_master_bus_delete(i2c_master_bus_handle_t bus);
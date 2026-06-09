#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t si5351_init(i2c_master_bus_handle_t bus);
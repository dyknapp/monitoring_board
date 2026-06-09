#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t bringup_init(void);
i2c_master_bus_handle_t bringup_get_i2c_bus(void);
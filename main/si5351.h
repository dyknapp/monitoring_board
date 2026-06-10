#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"


/**
 * @brief Initialize the Si5351 clock generator and start 32.768kHz output
 * * @param bus Handle to the initialized I2C master bus
 * @return esp_err_t ESP_OK on success, otherwise appropriate error code
 */
esp_err_t si5351_init(i2c_master_bus_handle_t bus);
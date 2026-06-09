#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    bool present[128];
    int devices_found;
} i2c_scan_result_t;

esp_err_t i2c_scan_bus(i2c_master_bus_handle_t bus,
                       i2c_scan_result_t *result,
                       int probe_timeout_ms);

bool i2c_scan_addr_present(const i2c_scan_result_t *result, uint8_t addr);
void i2c_scan_print(const i2c_scan_result_t *result);
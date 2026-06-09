#include "i2c_scan.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "i2c_scan";

esp_err_t i2c_scan_bus(i2c_master_bus_handle_t bus,
                       i2c_scan_result_t *result,
                       int probe_timeout_ms)
{
    if (bus == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));

    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        esp_err_t err = i2c_master_probe(bus, addr, probe_timeout_ms);

        if (err == ESP_OK) {
            result->present[addr] = true;
            result->devices_found++;
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Timeout probing address 0x%02X; bus may be hung", addr);
        }
    }

    return ESP_OK;
}

bool i2c_scan_addr_present(const i2c_scan_result_t *result, uint8_t addr)
{
    if (result == NULL || addr >= 128) {
        return false;
    }

    return result->present[addr];
}

void i2c_scan_print(const i2c_scan_result_t *result)
{
    if (result == NULL) {
        return;
    }

    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    printf("00:         ");

    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        if (addr % 16 == 0) {
            printf("\n%02x:", addr);
        }

        if (result->present[addr]) {
            printf(" %02x", addr);
        } else {
            printf(" --");
        }
    }

    printf("\n\n");
}
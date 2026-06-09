#include "si5351.h"

#include "board_config.h"
#include "esp_log.h"

static const char *TAG = "si5351";

esp_err_t si5351_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Si5351 init placeholder at 0x%02X", SI5351_I2C_ADDR);

    // Later:
    // - add Si5351 as an i2c_master_dev_handle_t
    // - disable outputs
    // - write PLL/MS registers
    // - enable outputs

    return ESP_OK;
}
#include "bringup.h"

#include "board_config.h"
#include "board_i2c.h"
#include "i2c_scan.h"
#include "mcp4728_addr.h"
#include "si5351.h"
#include "freq_counter.h"
#include "eth_bringup.h"
#include "rmii_loopback.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/clk_tree_defs.h"  // Defines SOC_RTC_SLOW_CLK_SRC_OSC_SLOW
#include "hal/clk_tree_ll.h"    // Provides clk_ll_rtc_slow_set_src

static const char *TAG = "bringup";

static i2c_master_bus_handle_t s_i2c_bus = NULL;

i2c_master_bus_handle_t bringup_get_i2c_bus(void)
{
    return s_i2c_bus;
}

static esp_err_t scan_current_bus(i2c_scan_result_t *scan)
{
    ESP_RETURN_ON_ERROR(i2c_scan_bus(s_i2c_bus, scan, 100), TAG, "I2C scan failed");

    i2c_scan_print(scan);

    ESP_LOGI(TAG, "I2C devices found: %d", scan->devices_found);

    return ESP_OK;
}

static esp_err_t ensure_mcp4728_target_address(void)
{
    i2c_scan_result_t scan = {0};

    ESP_RETURN_ON_ERROR(scan_current_bus(&scan), TAG, "Initial scan failed");

    if (i2c_scan_addr_present(&scan, MCP4728_TARGET_ADDR)) {
        ESP_LOGI(TAG, "Address 0x%02X is already present; skipping MCP4728 EEPROM write",
                 MCP4728_TARGET_ADDR);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Address 0x%02X not present; attempting MCP4728 address programming",
             MCP4728_TARGET_ADDR);

    if (!i2c_scan_addr_present(&scan, MCP4728_DEFAULT_ADDR)) {
        ESP_LOGE(TAG,
                 "Neither 0x%02X nor 0x%02X is present. Cannot safely program MCP4728 address.",
                 MCP4728_DEFAULT_ADDR,
                 MCP4728_TARGET_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * The hardware I2C driver owns SDA/SCL. Delete it before bit-banging
     * the MCP4728 address-programming sequence.
     */
    ESP_RETURN_ON_ERROR(board_i2c_master_bus_delete(s_i2c_bus),
                        TAG,
                        "Failed to delete I2C bus before MCP4728 programming");
    s_i2c_bus = NULL;

    ESP_RETURN_ON_ERROR(mcp4728_program_address_bits(MCP4728_DEFAULT_ADDR_BITS,
                                                     MCP4728_TARGET_ADDR_BITS),
                        TAG,
                        "MCP4728 address programming failed");

    /*
     * Recreate the normal hardware I2C bus and rescan.
     */
    ESP_RETURN_ON_ERROR(board_i2c_master_bus_create(&s_i2c_bus),
                        TAG,
                        "Failed to recreate I2C bus");

    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(scan_current_bus(&scan), TAG, "Post-program scan failed");

    if (!i2c_scan_addr_present(&scan, MCP4728_TARGET_ADDR)) {
        ESP_LOGE(TAG, "MCP4728 target address 0x%02X still not present after programming",
                 MCP4728_TARGET_ADDR);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MCP4728 target address 0x%02X verified", MCP4728_TARGET_ADDR);

    return ESP_OK;
}

esp_err_t bringup_init(void)
{
    ESP_LOGI(TAG, "Creating I2C master bus");

    ESP_RETURN_ON_ERROR(board_i2c_master_bus_create(&s_i2c_bus),
                        TAG,
                        "Failed to create I2C bus");

    ESP_RETURN_ON_ERROR(ensure_mcp4728_target_address(),
                        TAG,
                        "MCP4728 address bring-up failed");

    /*
     * At this point:
     * - 0x61 should be MCP4728
     * - 0x60 should be Si5351, assuming it is populated and powered
     * - hardware I2C bus remains alive for normal drivers
     */
    ESP_RETURN_ON_ERROR(si5351_init(s_i2c_bus),
                        TAG,
                        "Si5351 initialization failed");
    xTaskCreate(frequency_measure_task, "freq_meas_task", 4096, NULL, 5, NULL);

    // Allow a short period for the newly generated 32.768 kHz clock to stabilize
    vTaskDelay(pdMS_TO_TICKS(50));

    // Switch the RTC slow clock source to the external oscillator input (OSC_SLOW)
    ESP_LOGI(TAG, "Migrating RTC clock tree source to external Si5351 signal...");

    LP_AON_CLKRST.lp_clk_conf.slow_clk_sel = 3;

    // Checking connection to the IP101 PHY
    test_ip101_connection();

    // Run RMII PHY loopback test if the Ethernet driver was installed successfully.
    {
        esp_eth_handle_t eth = eth_bringup_get_handle();
        if (eth) {
            esp_err_t loop_ret = test_rmii_phy_loopback(eth);
            if (loop_ret != ESP_OK) {
                ESP_LOGE(TAG, "RMII loopback test failed: %s", esp_err_to_name(loop_ret));
            }
        } else {
            ESP_LOGW(TAG, "Ethernet handle not available; skipping RMII loopback test");
        }
    }

    ESP_LOGI(TAG, "Board bring-up complete");

    return ESP_OK;
}

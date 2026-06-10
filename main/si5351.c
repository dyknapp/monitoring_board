#include "si5351.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "si5351";

// Keep a handle to the device so we can use it in other functions later
static i2c_master_dev_handle_t s_si5351_dev = NULL;

// Helper to write a single register
static esp_err_t si5351_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_si5351_dev, buf, sizeof(buf), -1);
}

static esp_err_t si5451_check_status(i2c_master_bus_handle_t bus, i2c_device_config_t *dev_cfg)
{
    // Read Register 0 (Device Status)
    uint8_t reg0_addr = 0x00;
    uint8_t status = 0;

    // We use a timeout of -1 to let the driver wait indefinitely
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_si5351_dev, &reg0_addr, 1, &status, 1, -1),
                        TAG, "Failed to read device status (Register 0)");

    // Parse and print the information
    ESP_LOGI(TAG, "Si5351 Device Status (Register 0): 0x%02X", status);
    ESP_LOGI(TAG, "  SYS_INIT  (System Init):     %d", (status >> 7) & 0x01);
    ESP_LOGI(TAG, "  LOL_B     (PLLB Loss Lock):  %d", (status >> 6) & 0x01);
    ESP_LOGI(TAG, "  LOL_A     (PLLA Loss Lock):  %d", (status >> 5) & 0x01);
    ESP_LOGI(TAG, "  LOS_CLKIN (CLKIN Loss Sig):  %d", (status >> 4) & 0x01);
    ESP_LOGI(TAG, "  LOS_XTAL  (XTAL Loss Sig):   %d", (status >> 3) & 0x01);
    ESP_LOGI(TAG, "  REVID     (Revision ID):     %d", status & 0x03);

    return ESP_OK;
}

esp_err_t si5351_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing Si5351 at 0x%02X", SI5351_I2C_ADDR);

    // 1. Add the Si5351 device to the existing I2C bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SI5351_I2C_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_si5351_dev), 
                        TAG, "Failed to add Si5351 device to I2C bus");

    ESP_RETURN_ON_ERROR(si5451_check_status(bus, &dev_cfg), TAG, "Failed to check Si5351 status");

    ESP_RETURN_ON_ERROR(si5351_write_reg(3, 0xFF), TAG, "Failed to disable outputs");

    // Power down all output drivers initially (Registers 16-23 are CLKs 0-7)
    // Set bit 7 to 1 to power down the output driver.
    for (uint8_t i = 16; i <= 23; i++) {
        si5351_write_reg(i, 0x80); 
    }

    // To produce 32.768kHz for RTC clock:
    // 10 MHz * 80 = 800 MHz
    // 800MHz / 24414.0625 = 32.768kHz

    // Configure PLLA for 800 MHz (10 MHz * 80)
    // f_VCO = f_XTAL x (a + b/c)
    // a=80, b=0, c=1. MSNA_P1 = 128*80 + 0 - 512 = 9728 (0x2600)
    // MSNA_P2 = 0, MSNA_P3 = 1
    si5351_write_reg(26, 0x00); // MSNA_P3[15:8]
    si5351_write_reg(27, 0x01); // MSNA_P3[7:0]
    si5351_write_reg(28, 0x00); // MSNA_P1[17:16]
    si5351_write_reg(29, 0x26); // MSNA_P1[15:8]
    si5351_write_reg(30, 0x00); // MSNA_P1[7:0]
    si5351_write_reg(31, 0x00); // MSNA_P3[19:16] | MSNA_P2[19:16]
    si5351_write_reg(32, 0x00); // MSNA_P2[15:8]
    si5351_write_reg(33, 0x00); // MSNA_P2[7:0]

    // Configure Multisynth 2 for 32.768 kHz
    // a=381, b=481, c=1024. 
    // Floor(128 * 481/1024) = 60
    // MS0_P1 = 128*381 + 60 - 512 = 48316 (0xBCBC)
    // MS0_P2 = 128*481 - 1024*60 = 128 (0x00080)
    // MS0_P3 = 1024 (0x00400)
    si5351_write_reg(58, 0x04); // MS2_P3[15:8]
    si5351_write_reg(59, 0x00); // MS2_P3[7:0]
    si5351_write_reg(60, 0x60); // Reg 60: R2_DIV (6:4) = 110b (Div by 64), MS2_DIVBY4 (3:2) = 00b, P2[17:16] = 00b
    si5351_write_reg(61, 0xBC); // MS2_P1[15:8]
    si5351_write_reg(62, 0xBC); // MS2_P1[7:0]
    si5351_write_reg(63, 0x00); // MS2_P3[19:16] | MS2_P2[19:16]
    si5351_write_reg(64, 0x00); // MS2_P2[15:8]
    si5351_write_reg(65, 0x80); // MS2_P2[7:0]

    // Configure CLK2 output control (Register 18)
    // Power up (0), Integer mode false (0), Source PLLA (0), No invert (0), 
    // Source MS2 (11), 8mA drive strength (11) -> 0x0F
    si5351_write_reg(18, 0x0F);

    // Reset PLLA to apply the new divider settings (Register 177)
    // Bit 5 is PLLA_RST
    si5351_write_reg(177, 0x20);

    // Enable CLK2 (Register 3)
    // Bit 0 = 0 (enabled), all other bits = 1 (disabled)
    si5351_write_reg(3, 0xFB);

    ESP_LOGI(TAG, "CLK2 configured for 32.768 kHz");

    // Wait for the Si5351 to initialize itself
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(si5451_check_status(bus, &dev_cfg), TAG, "Failed to check Si5351 status");

    return ESP_OK;
}
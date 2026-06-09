#include "mcp4728_addr.h"

#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp4728_addr";

#define I2C_DELAY_US 5
#define I2C_DELAY() esp_rom_delay_us(I2C_DELAY_US)

static void bb_i2c_init(void)
{
    gpio_reset_pin(BOARD_I2C_SDA_IO);
    gpio_reset_pin(BOARD_I2C_SCL_IO);
    gpio_reset_pin(BOARD_MCP4728_LDAC_IO);

    gpio_set_direction(BOARD_I2C_SDA_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(BOARD_I2C_SCL_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(BOARD_MCP4728_LDAC_IO, GPIO_MODE_OUTPUT);

    gpio_pullup_en(BOARD_I2C_SDA_IO);
    gpio_pullup_en(BOARD_I2C_SCL_IO);

    gpio_set_level(BOARD_I2C_SDA_IO, 1);
    gpio_set_level(BOARD_I2C_SCL_IO, 1);
    gpio_set_level(BOARD_MCP4728_LDAC_IO, 1);
}

static void bb_i2c_start(void)
{
    gpio_set_level(BOARD_I2C_SDA_IO, 1);
    gpio_set_level(BOARD_I2C_SCL_IO, 1);
    I2C_DELAY();

    gpio_set_level(BOARD_I2C_SDA_IO, 0);
    I2C_DELAY();

    gpio_set_level(BOARD_I2C_SCL_IO, 0);
    I2C_DELAY();
}

static void bb_i2c_stop(void)
{
    gpio_set_level(BOARD_I2C_SDA_IO, 0);
    I2C_DELAY();

    gpio_set_level(BOARD_I2C_SCL_IO, 1);
    I2C_DELAY();

    gpio_set_level(BOARD_I2C_SDA_IO, 1);
    I2C_DELAY();
}

static int bb_i2c_write_byte(uint8_t data, int byte_index)
{
    for (int bit = 7; bit >= 0; bit--) {
        gpio_set_level(BOARD_I2C_SDA_IO, (data >> bit) & 1);
        I2C_DELAY();

        gpio_set_level(BOARD_I2C_SCL_IO, 1);
        I2C_DELAY();

        gpio_set_level(BOARD_I2C_SCL_IO, 0);

        /*
         * MCP4728 address-programming LDAC timing:
         * LDAC falling edge after the 8th data bit of byte 2.
         */
        if (byte_index == 2 && bit == 0) {
            gpio_set_level(BOARD_MCP4728_LDAC_IO, 0);
        }

        I2C_DELAY();
    }

    // Release SDA for ACK.
    gpio_set_level(BOARD_I2C_SDA_IO, 1);
    I2C_DELAY();

    gpio_set_level(BOARD_I2C_SCL_IO, 1);
    I2C_DELAY();

    int ack = gpio_get_level(BOARD_I2C_SDA_IO);

    gpio_set_level(BOARD_I2C_SCL_IO, 0);
    I2C_DELAY();

    /*
     * Conservative release: keep LDAC low through byte-3 ACK.
     * This is the timing that fixed your write.
     */
    if (byte_index == 3) {
        gpio_set_level(BOARD_MCP4728_LDAC_IO, 1);
        I2C_DELAY();
    }

    return ack; // 0 = ACK, 1 = NACK
}

esp_err_t mcp4728_program_address_bits(uint8_t current_bits, uint8_t new_bits)
{
    current_bits &= 0x07;
    new_bits &= 0x07;

    const uint8_t byte1 = 0xC0 | (current_bits << 1);
    const uint8_t byte2 = 0x61 | (current_bits << 2);
    const uint8_t byte3 = 0x62 | (new_bits << 2);
    const uint8_t byte4 = 0x63 | (new_bits << 2);

    ESP_LOGW(TAG, "Programming MCP4728 address bits %u -> %u", current_bits, new_bits);
    ESP_LOGI(TAG, "Address command bytes: 0x%02X 0x%02X 0x%02X 0x%02X",
             byte1, byte2, byte3, byte4);

    bb_i2c_init();
    vTaskDelay(pdMS_TO_TICKS(50));

    bb_i2c_start();

    int ack1 = bb_i2c_write_byte(byte1, 1);
    int ack2 = bb_i2c_write_byte(byte2, 2);
    int ack3 = bb_i2c_write_byte(byte3, 3);
    int ack4 = bb_i2c_write_byte(byte4, 4);

    bb_i2c_stop();

    gpio_set_level(BOARD_MCP4728_LDAC_IO, 1);

    ESP_LOGI(TAG, "ACKs: %d %d %d %d", ack1, ack2, ack3, ack4);
    ESP_LOGW(TAG, "ACKs may be masked by another device at 0x60, such as Si5351");

    /*
     * MCP4728 EEPROM write max is 50 ms.
     * Give it margin before returning to hardware I2C.
     */
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}
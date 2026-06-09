#include "board_i2c.h"
#include "board_config.h"

esp_err_t board_i2c_master_bus_create(i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_config_t config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_PORT,
        .scl_io_num = BOARD_I2C_SCL_IO,
        .sda_io_num = BOARD_I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&config, out_bus);
}

esp_err_t board_i2c_master_bus_delete(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_OK;
    }

    return i2c_del_master_bus(bus);
}
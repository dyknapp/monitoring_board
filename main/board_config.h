#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define BOARD_I2C_PORT              I2C_NUM_0
#define BOARD_I2C_SCL_IO            GPIO_NUM_16
#define BOARD_I2C_SDA_IO            GPIO_NUM_17
#define BOARD_MCP4728_LDAC_IO       GPIO_NUM_49

#define BOARD_I2C_FREQ_HZ           100000

#define SI5351_I2C_ADDR             0x60

#define MCP4728_DEFAULT_ADDR        0x60
#define MCP4728_TARGET_ADDR         0x61

#define MCP4728_DEFAULT_ADDR_BITS   0
#define MCP4728_TARGET_ADDR_BITS    1
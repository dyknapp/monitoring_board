#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t mcp4728_program_address_bits(uint8_t current_bits, uint8_t new_bits);
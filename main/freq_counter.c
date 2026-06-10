#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"

static const char *FREQ_TAG = "freq_meas";

// We use GPIO 0 as the input pin for our 32.768 kHz signal
#define FREQ_MEASURE_GPIO 0
#define MEASURE_WINDOW_MS 500 

void frequency_measure_task(void *arg)
{
    ESP_LOGI(FREQ_TAG, "Initializing Pulse Counter on GPIO %d", FREQ_MEASURE_GPIO);

    // 1. Configure the PCNT Unit
    pcnt_unit_config_t unit_config = {
        .high_limit = 30000, // Stay under the 16-bit signed limit (32767)
        .low_limit = -1,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Optional: Add a small glitch filter (e.g., 1000 ns) to ignore noise
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 2. Configure the PCNT Channel
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = FREQ_MEASURE_GPIO,
        .level_gpio_num = -1, // We don't need a control signal, just count edges
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    // 3. Set the edge action: Increase on rising edge, hold on falling edge
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, 
                                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE, 
                                                 PCNT_CHANNEL_EDGE_ACTION_HOLD));

    // 4. Enable and start the counter
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    ESP_LOGI(FREQ_TAG, "PCNT started. Entering measurement loop.");

    // Clear the counter and start the timer
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    int64_t t_start = esp_timer_get_time(); // Time in microseconds
    
    // Wait roughly 500 ms
    vTaskDelay(pdMS_TO_TICKS(MEASURE_WINDOW_MS));
    
    // Stop the timer and grab the pulse count
    int64_t t_end = esp_timer_get_time();
    int pulse_count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));

    // Calculate frequency: Freq = Pulses / Time(s)
    float exact_time_s = (float)(t_end - t_start) / 1000000.0f;
    float freq = (float)pulse_count / exact_time_s;

    ESP_LOGI(FREQ_TAG, "Counted %d pulses in %.4f seconds -> Measured Frequency: %.2f Hz", 
                pulse_count, exact_time_s, freq);

    vTaskDelete(NULL);
}

#include "esp_eth.h"
#include "esp_eth_phy_ip101.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define RMII_TX_EN_GPIO  40
#define RMII_TXD0_GPIO   34
#define RMII_TXD1_GPIO   35
#define RMII_CRS_DV_GPIO 28
#define RMII_RXD0_GPIO   29
#define RMII_RXD1_GPIO   30
#define CONFIG_ETHERNET_MDC_GPIO  27
#define CONFIG_ETHERNET_MDIO_GPIO 26
#define ETH_RMII_CLK_GPIO GPIO_NUM_32
#define ETH_PHY_RST_GPIO  11
#define ETH_PHY_ADDR      -1 // -1 means auto-detect

static const char *TAG = "eth_bringup";
static esp_eth_handle_t s_eth_handle = NULL;

void test_ip101_connection(void)
{
    ESP_LOGI(TAG, "Initializing Ethernet MAC and PHY...");

    // Initialize MAC Configuration
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();                      // apply default common MAC configuration
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG(); // apply default vendor-specific MAC configuration
    esp32_emac_config.emac_dataif_gpio.rmii.tx_en_num  = RMII_TX_EN_GPIO;        // alter the GPIO used for TX_EN signal
    esp32_emac_config.emac_dataif_gpio.rmii.txd0_num   = RMII_TXD0_GPIO;         // alter the GPIO used for TXD0 signal
    esp32_emac_config.emac_dataif_gpio.rmii.txd1_num   = RMII_TXD1_GPIO;         // alter the GPIO used for TXD1 signal
    esp32_emac_config.emac_dataif_gpio.rmii.crs_dv_num = RMII_CRS_DV_GPIO;       // alter the GPIO used for CRS_DV signal
    esp32_emac_config.emac_dataif_gpio.rmii.rxd0_num   = RMII_RXD0_GPIO;         // alter the GPIO used for RXD0 signal
    esp32_emac_config.emac_dataif_gpio.rmii.rxd1_num   = RMII_RXD1_GPIO;         // alter the GPIO used for RXD1 signal
    esp32_emac_config.smi_gpio.mdc_num = CONFIG_ETHERNET_MDC_GPIO;               // alter the GPIO used for MDC signal
    esp32_emac_config.smi_gpio.mdio_num = CONFIG_ETHERNET_MDIO_GPIO;             // alter the GPIO used for MDIO signal
    esp32_emac_config.clock_config.rmii.clock_gpio = ETH_RMII_CLK_GPIO;          // alter the GPIO used for clock signal
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;            // use external clock (Si5351)
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config); // create MAC instance
    if (mac == NULL) {
        ESP_LOGE(TAG, "Failed to create MAC instance");
        return;
    }
    

    // Initialize PHY Configuration for IP101
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;
    
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (phy == NULL) {
        ESP_LOGE(TAG, "Failed to create PHY instance");
        mac->del(mac);
        return;
    }

    // Install the Driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    
    esp_err_t ret = esp_eth_driver_install(&config, &eth_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SUCCESS! ESP32-P4 successfully communicated with the IP101 PHY.");
        s_eth_handle = eth_handle;
        // Driver installed successfully, meaning the OUI was read correctly.
    } else {
        ESP_LOGE(TAG, "FAILED to install Ethernet driver. Error: %s", esp_err_to_name(ret));
    }
}

esp_eth_handle_t eth_bringup_get_handle(void)
{
    return s_eth_handle;
}
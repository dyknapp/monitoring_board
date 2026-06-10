#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_com.h"
#include "esp_event.h"
#include "esp_log.h"
#include "eth_phy_802_3_regs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "rmii_loop";

#define RMII_LOOP_STARTED_BIT   BIT0
#define RMII_LOOP_CONNECTED_BIT BIT1
#define RMII_LOOP_STOPPED_BIT   BIT2
#define RMII_LOOP_RX_BIT        BIT3

#define RMII_LOOP_LINK_TIMEOUT_MS 5000
#define RMII_LOOP_RX_TIMEOUT_MS   1000

#define IP101_PCR_REG_ADDR       0x14
#define IP101_CSSR_REG_ADDR      0x1E
#define IP101_PSCR_REG_ADDR      0x11
#define IP101_PAGE_PHY_SPEC_CTRL 1
#define IP101_PAGE_STATUS        16
#define IP101_FORCE_LINK_100     (1U << 7)
#define IP101_FORCE_LINK_10      (1U << 8)

static uint8_t s_expected_payload[64];

typedef struct {
    EventGroupHandle_t event_group;
} rmii_loopback_rx_ctx_t;

static esp_err_t rmii_loopback_read_phy_reg(esp_eth_handle_t eth_handle,
                                            uint32_t reg_addr,
                                            uint32_t *reg_value)
{
    esp_eth_phy_reg_rw_data_t reg = {
        .reg_addr = reg_addr,
        .reg_value_p = reg_value,
    };
    return esp_eth_ioctl(eth_handle, ETH_CMD_READ_PHY_REG, &reg);
}

static esp_err_t rmii_loopback_write_phy_reg(esp_eth_handle_t eth_handle,
                                             uint32_t reg_addr,
                                             uint32_t reg_value)
{
    esp_eth_phy_reg_rw_data_t reg = {
        .reg_addr = reg_addr,
        .reg_value_p = &reg_value,
    };
    return esp_eth_ioctl(eth_handle, ETH_CMD_WRITE_PHY_REG, &reg);
}

static esp_err_t ip101_select_page(esp_eth_handle_t eth_handle, uint32_t page)
{
    return rmii_loopback_write_phy_reg(eth_handle, IP101_PCR_REG_ADDR, page & 0x1f);
}

static esp_err_t ip101_force_loopback_link(esp_eth_handle_t eth_handle,
                                           eth_speed_t speed,
                                           bool enable)
{
    esp_err_t ret = ESP_OK;
    uint32_t pscr = 0;

    ESP_GOTO_ON_ERROR(ip101_select_page(eth_handle, IP101_PAGE_PHY_SPEC_CTRL),
                      cleanup,
                      TAG,
                      "select IP101 page 1 failed");
    ESP_GOTO_ON_ERROR(rmii_loopback_read_phy_reg(eth_handle, IP101_PSCR_REG_ADDR, &pscr),
                      cleanup,
                      TAG,
                      "read IP101 PSCR failed");

    pscr &= ~(IP101_FORCE_LINK_100 | IP101_FORCE_LINK_10);
    if (enable) {
        pscr |= (speed == ETH_SPEED_100M) ? IP101_FORCE_LINK_100 : IP101_FORCE_LINK_10;
    }

    ESP_GOTO_ON_ERROR(rmii_loopback_write_phy_reg(eth_handle, IP101_PSCR_REG_ADDR, pscr),
                      cleanup,
                      TAG,
                      "write IP101 PSCR failed");

cleanup:
    (void)ip101_select_page(eth_handle, IP101_PAGE_STATUS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "IP101 forced link %s for PHY loopback at %d Mbps",
                 enable ? "enabled" : "disabled",
                 speed == ETH_SPEED_100M ? 100 : 10);
    }
    return ret;
}

static void rmii_loopback_dump_phy_state(esp_eth_handle_t eth_handle, const char *label)
{
    uint32_t bmcr = 0;
    uint32_t bmsr = 0;
    uint32_t cssr = 0;
    uint32_t pscr = 0;

    if (rmii_loopback_read_phy_reg(eth_handle, ETH_PHY_BMCR_REG_ADDR, &bmcr) == ESP_OK &&
        rmii_loopback_read_phy_reg(eth_handle, ETH_PHY_BMSR_REG_ADDR, &bmsr) == ESP_OK) {
        ESP_LOGI(TAG,
                 "%s BMCR=0x%04" PRIx32 " BMSR=0x%04" PRIx32
                 " loop=%" PRIu32 " an=%" PRIu32 " speed=%" PRIu32 " duplex=%" PRIu32
                 " bmsr_link=%" PRIu32,
                 label,
                 bmcr,
                 bmsr,
                 (bmcr >> 14) & 1,
                 (bmcr >> 12) & 1,
                 (bmcr >> 13) & 1,
                 (bmcr >> 8) & 1,
                 (bmsr >> 2) & 1);
    }

    if (ip101_select_page(eth_handle, IP101_PAGE_STATUS) == ESP_OK &&
        rmii_loopback_read_phy_reg(eth_handle, IP101_CSSR_REG_ADDR, &cssr) == ESP_OK) {
        ESP_LOGI(TAG,
                 "%s IP101 CSSR=0x%04" PRIx32 " link=%" PRIu32 " op_mode=%" PRIu32,
                 label,
                 cssr,
                 (cssr >> 8) & 1,
                 cssr & 0x7);
    }

    if (ip101_select_page(eth_handle, IP101_PAGE_PHY_SPEC_CTRL) == ESP_OK &&
        rmii_loopback_read_phy_reg(eth_handle, IP101_PSCR_REG_ADDR, &pscr) == ESP_OK) {
        ESP_LOGI(TAG,
                 "%s IP101 PSCR=0x%04" PRIx32 " force100=%" PRIu32 " force10=%" PRIu32,
                 label,
                 pscr,
                 (pscr >> 7) & 1,
                 (pscr >> 8) & 1);
    }

    (void)ip101_select_page(eth_handle, IP101_PAGE_STATUS);
}

static void rmii_loopback_eth_event_handler(void *arg,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            void *event_data)
{
    EventGroupHandle_t event_group = (EventGroupHandle_t)arg;

    switch (event_id) {
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet driver started");
        xEventGroupSetBits(event_group, RMII_LOOP_STARTED_BIT);
        break;
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        xEventGroupSetBits(event_group, RMII_LOOP_CONNECTED_BIT);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet driver stopped");
        xEventGroupSetBits(event_group, RMII_LOOP_STOPPED_BIT);
        break;
    default:
        break;
    }
}

static esp_err_t rmii_loopback_rx_cb(esp_eth_handle_t eth_handle,
                                     uint8_t *buffer,
                                     uint32_t length,
                                     void *priv)
{
    rmii_loopback_rx_ctx_t *ctx = (rmii_loopback_rx_ctx_t *)priv;

    if (length >= sizeof(s_expected_payload) &&
        buffer[12] == 0x88 &&
        buffer[13] == 0xB5 &&
        memcmp(buffer + 14, s_expected_payload + 14, sizeof(s_expected_payload) - 14) == 0) {
        ESP_LOGI(TAG, "RMII loopback frame received, len=%" PRIu32, length);
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupSetBits(ctx->event_group, RMII_LOOP_RX_BIT);
        }
    } else {
        ESP_LOGW(TAG, "Unexpected Ethernet frame received, len=%" PRIu32, length);
    }

    free(buffer);
    return ESP_OK;
}

esp_err_t test_rmii_phy_loopback(esp_eth_handle_t eth_handle)
{
    ESP_RETURN_ON_FALSE(eth_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Ethernet handle is null");

    esp_err_t ret = ESP_OK;
    EventGroupHandle_t event_group = NULL;
    esp_event_handler_instance_t event_instance = NULL;
    bool driver_started = false;
    rmii_loopback_rx_ctx_t rx_ctx = {0};
    eth_speed_t speed = ETH_SPEED_100M;
    eth_duplex_t duplex = ETH_DUPLEX_FULL;

    ESP_LOGI(TAG, "Configuring PHY loopback test");

    event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(event_group != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create event group");
    rx_ctx.event_group = event_group;

    ret = esp_event_handler_instance_register(ETH_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              rmii_loopback_eth_event_handler,
                                              event_group,
                                              &event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Ethernet event handler: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    bool autoneg = false;
    ret = esp_eth_ioctl(eth_handle, ETH_CMD_S_AUTONEGO, &autoneg);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Ethernet driver is already started; stopping before loopback reconfiguration");
        ESP_GOTO_ON_ERROR(esp_eth_stop(eth_handle), cleanup, TAG, "esp_eth_stop failed");
        (void)xEventGroupWaitBits(event_group,
                                  RMII_LOOP_STOPPED_BIT,
                                  pdTRUE,
                                  pdTRUE,
                                  pdMS_TO_TICKS(1000));
        ret = esp_eth_ioctl(eth_handle, ETH_CMD_S_AUTONEGO, &autoneg);
    }
    ESP_GOTO_ON_ERROR(ret, cleanup, TAG, "disable autonegotiation failed");

    ESP_GOTO_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_S_SPEED, &speed),
                      cleanup,
                      TAG,
                      "set speed failed");
    ESP_GOTO_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_S_DUPLEX_MODE, &duplex),
                      cleanup,
                      TAG,
                      "set duplex failed");

    bool loopback = true;
    ESP_GOTO_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_S_PHY_LOOPBACK, &loopback),
                      cleanup,
                      TAG,
                      "enable PHY loopback failed");

    /*
     * IP101 does not always report link-up from BMCR loopback alone. Its vendor
     * PSCR force-link bits make the ESP Ethernet driver state machine permit TX.
     */
    ESP_GOTO_ON_ERROR(ip101_force_loopback_link(eth_handle, speed, true),
                      cleanup,
                      TAG,
                      "force IP101 loopback link failed");

    bool promisc = true;
    ESP_GOTO_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_S_PROMISCUOUS, &promisc),
                      cleanup,
                      TAG,
                      "enable promiscuous mode failed");
    ESP_GOTO_ON_ERROR(esp_eth_update_input_path(eth_handle, rmii_loopback_rx_cb, &rx_ctx),
                      cleanup,
                      TAG,
                      "set Ethernet input path failed");

    uint8_t mac[6] = {0};
    ESP_GOTO_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac),
                      cleanup,
                      TAG,
                      "get MAC address failed");

    memset(s_expected_payload, 0, sizeof(s_expected_payload));
    memcpy(&s_expected_payload[0], mac, 6);
    memcpy(&s_expected_payload[6], mac, 6);
    s_expected_payload[12] = 0x88;
    s_expected_payload[13] = 0xB5;

    for (int i = 14; i < sizeof(s_expected_payload); i++) {
        s_expected_payload[i] = (uint8_t)(0xA5 ^ i);
    }

    rmii_loopback_dump_phy_state(eth_handle, "before start");

    ESP_GOTO_ON_ERROR(esp_eth_start(eth_handle), cleanup, TAG, "esp_eth_start failed");
    driver_started = true;

    EventBits_t bits = xEventGroupWaitBits(event_group,
                                           RMII_LOOP_STARTED_BIT | RMII_LOOP_CONNECTED_BIT,
                                           pdFALSE,
                                           pdTRUE,
                                           pdMS_TO_TICKS(RMII_LOOP_LINK_TIMEOUT_MS));
    if ((bits & RMII_LOOP_CONNECTED_BIT) == 0) {
        rmii_loopback_dump_phy_state(eth_handle, "link timeout");
        ESP_LOGE(TAG, "Timed out waiting for PHY loopback link-up");
        ret = ESP_ERR_TIMEOUT;
        goto cleanup;
    }

    rmii_loopback_dump_phy_state(eth_handle, "after link up");
    xEventGroupClearBits(event_group, RMII_LOOP_RX_BIT);

    ret = esp_eth_transmit(eth_handle, s_expected_payload, sizeof(s_expected_payload));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_transmit failed: %s", esp_err_to_name(ret));
        rmii_loopback_dump_phy_state(eth_handle, "transmit failed");
        goto cleanup;
    }

    bits = xEventGroupWaitBits(event_group,
                               RMII_LOOP_RX_BIT,
                               pdTRUE,
                               pdTRUE,
                               pdMS_TO_TICKS(RMII_LOOP_RX_TIMEOUT_MS));
    if ((bits & RMII_LOOP_RX_BIT) != 0) {
        ESP_LOGI(TAG, "PASS: RMII TX and RX digital data path looped successfully.");
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "FAIL: transmitted frame was not received back.");
        rmii_loopback_dump_phy_state(eth_handle, "rx timeout");
        ret = ESP_ERR_TIMEOUT;
    }

cleanup:
    if (driver_started) {
        esp_err_t stop_ret = esp_eth_stop(eth_handle);
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_eth_stop during cleanup failed: %s", esp_err_to_name(stop_ret));
        } else if (event_group != NULL) {
            (void)xEventGroupWaitBits(event_group,
                                      RMII_LOOP_STOPPED_BIT,
                                      pdTRUE,
                                      pdTRUE,
                                      pdMS_TO_TICKS(1000));
        }
    }

    (void)esp_eth_update_input_path(eth_handle, NULL, NULL);

    bool promisc_off = false;
    (void)esp_eth_ioctl(eth_handle, ETH_CMD_S_PROMISCUOUS, &promisc_off);

    bool loopback_off = false;
    (void)esp_eth_ioctl(eth_handle, ETH_CMD_S_PHY_LOOPBACK, &loopback_off);
    (void)ip101_force_loopback_link(eth_handle, speed, false);

    if (event_instance != NULL) {
        (void)esp_event_handler_instance_unregister(ETH_EVENT,
                                                   ESP_EVENT_ANY_ID,
                                                   event_instance);
    }
    if (event_group != NULL) {
        vEventGroupDelete(event_group);
    }

    return ret;
}

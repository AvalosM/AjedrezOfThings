#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "radio.h"

static const char *TAG = "radio";

const uint8_t RADIO_BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static char s_name[LINK_NAME_LEN];
static uint8_t s_mac[6];
static uint8_t s_peer[6];   /* the one board registered for unicast */
static QueueHandle_t s_rxq;

/* Runs in the Wi-Fi task: drop frames that aren't ours, copy the rest, return. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const frame_t *f = (const frame_t *)data;
    if (len < (int)sizeof(frame_t) || len > (int)FRAME_MAX_LEN ||
        memcmp(f->magic, FRAME_MAGIC, 3) != 0 || f->version != FRAME_VERSION) {
        return;
    }

    frame_rx_t rx;
    memcpy(rx.mac, info->src_addr, 6);
    rx.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    rx.len = len;
    memcpy(rx.buf, data, len);
    xQueueSend(s_rxq, &rx, 0);  /* queue full: drop it, never block the Wi-Fi task */
}

static esp_err_t add_peer(const uint8_t mac[6])
{
    esp_now_peer_info_t peer = {.ifidx = WIFI_IF_STA, .encrypt = false};
    memcpy(peer.peer_addr, mac, 6);
    return esp_now_add_peer(&peer);
}

/* Wi-Fi in station mode, not connected to any access point: ESP-NOW only
 * needs the radio running on a fixed channel. */
static esp_err_t wifi_start(void)
{
    esp_err_t err = nvs_flash_init();  /* Wi-Fi keeps calibration data in NVS */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi power save");  /* no sleeping: less lost frames */
    return esp_wifi_set_channel(CONFIG_LINK_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

esp_err_t radio_init(const char *name, QueueHandle_t rxq)
{
    memcpy(s_name, name, LINK_NAME_LEN);
    s_rxq = rxq;

    ESP_RETURN_ON_ERROR(wifi_start(), TAG, "wifi");
    ESP_RETURN_ON_ERROR(esp_wifi_get_mac(WIFI_IF_STA, s_mac), TAG, "mac");
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "espnow init");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_recv), TAG, "recv cb");
    return add_peer(RADIO_BROADCAST);
}

void radio_set_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(s_peer)) {
        esp_now_del_peer(s_peer);
    }
    memcpy(s_peer, mac, 6);
    add_peer(mac);
}

esp_err_t radio_send(const uint8_t dest[6], frame_type_t type, bool busy, const void *data, size_t len)
{
    uint8_t buf[FRAME_MAX_LEN];
    frame_t *f = (frame_t *)buf;

    memcpy(f->magic, FRAME_MAGIC, 3);
    f->version = FRAME_VERSION;
    f->type = type;
    f->flags = busy ? FLAG_BUSY : 0;
    memcpy(f->name, s_name, LINK_NAME_LEN);
    memcpy(f->mac, s_mac, 6);
    if (len) {
        memcpy(f->data, data, len);
    }
    return esp_now_send(dest, buf, sizeof(frame_t) + len);
}

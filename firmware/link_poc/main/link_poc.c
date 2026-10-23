/*
 * link proof of concept: two boards find each other over ESP-NOW, swap
 * names, and stay connected.
 *
 * Flash the same build on two boards and power both: they pair on their own.
 *   KEY1  connect to the nearest free board
 *   KEY2  hang up
 * Every 2 s the connected boards send each other a short text message, so
 * you can see data flowing in the serial log.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "buttons.h"
#include "display.h"
#include "link.h"

static const char *TAG = "poc";

#define UI_PERIOD_MS   100
#define DATA_PERIOD_MS 2000

static char s_name[LINK_NAME_LEN + 1];
static volatile uint32_t s_rx_count;

/* Arcade-style initials from the MAC, so every board gets its own name. */
static void name_from_mac(const uint8_t mac[6], char out[LINK_NAME_LEN + 1])
{
    for (int i = 0; i < LINK_NAME_LEN; i++) {
        out[i] = 'A' + mac[3 + i] % 26;
    }
    out[LINK_NAME_LEN] = '\0';
}

static void on_link(link_event_t ev, const link_peer_t *peer, const uint8_t *data, size_t len)
{
    switch (ev) {
    case LINK_EVT_CONNECTED:
        ESP_LOGI(TAG, "CONNECTED to %s %02X:%02X:%02X:%02X:%02X:%02X", peer->name, peer->mac[0], peer->mac[1],
                 peer->mac[2], peer->mac[3], peer->mac[4], peer->mac[5]);
        break;
    case LINK_EVT_DISCONNECTED:
        ESP_LOGW(TAG, "DISCONNECTED from %s", peer->name);
        break;
    case LINK_EVT_DATA:
        s_rx_count++;
        ESP_LOGI(TAG, "from %s (%d dBm): %.*s", peer->name, peer->rssi, (int)len, (const char *)data);
        break;
    }
}

static void draw(void)
{
    char line[24];
    link_peer_t peer;
    link_peer_t near[4];
    const bool connected = link_get_peer(&peer);
    const int n = link_nearby(near, 4);

    display_clear();
    snprintf(line, sizeof(line), "LINK POC  me:%s", s_name);
    display_text(1, 1, line, true);

    if (connected) {
        snprintf(line, sizeof(line), "<> %s %02X%02X %ddBm", peer.name, peer.mac[4], peer.mac[5], peer.rssi);
        display_text(0, 13, line, false);
        snprintf(line, sizeof(line), "   rx msgs: %lu", (unsigned long)s_rx_count);
        display_text(0, 22, line, false);
    } else {
        display_text(0, 13, "searching...", false);
    }

    display_text(0, 33, "nearby:", false);
    for (int i = 0; i < n && i < 3; i++) {
        snprintf(line, sizeof(line), "%s %02X%02X %4d %s", near[i].name, near[i].mac[4], near[i].mac[5], near[i].rssi,
                 near[i].busy ? "busy" : "");
        display_text(0, 42 + i * 8, line, false);
    }
    display_flush();
}

void app_main(void)
{
    uint8_t mac[6];

    bool display_ok = display_init() == ESP_OK;
    ESP_ERROR_CHECK(buttons_init());

    /* Reads eFuse, works before Wi-Fi is up; same MAC link_get_mac() returns. */
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    name_from_mac(mac, s_name);
    ESP_LOGI(TAG, "I am %s", s_name);

    ESP_ERROR_CHECK(link_init(s_name, on_link));
    link_connect(NULL);

    QueueHandle_t buttons = buttons_event_queue();
    uint32_t next_data = 0;
    uint32_t seq = 0;
    while (1) {
        button_event_t ev;
        if (xQueueReceive(buttons, &ev, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdTRUE && ev.pressed) {
            if (ev.id == BTN_KEY1) {
                ESP_LOGI(TAG, "KEY1: connect to nearest");
                link_connect(NULL);
            } else if (ev.id == BTN_KEY2) {
                ESP_LOGI(TAG, "KEY2: hang up");
                link_disconnect();
            }
        }

        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (link_get_peer(NULL) && (int32_t)(now - next_data) >= 0) {
            char msg[32];
            int len = snprintf(msg, sizeof(msg), "hello #%lu from %s", (unsigned long)seq++, s_name);
            link_send(msg, len);
            next_data = now + DATA_PERIOD_MS;
        }

        if (display_ok) {
            draw();
        }
    }
}

#include <ctype.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "link.h"

static const char *TAG = "link";

#define HELLO_PERIOD_MS      1000
#define HELLO_JITTER_MS      200    /* so 20 boards don't all talk at once */
#define PING_PERIOD_MS       1000
#define JOIN_RETRY_MS        300
#define HANDSHAKE_TIMEOUT_MS 2000
#define PEER_TIMEOUT_MS      3000
#define NEARBY_TIMEOUT_MS    3000
#define RETRY_JITTER_MS      300    /* breaks ties when two boards retry together */
#define TICK_MS              50
#define RX_QUEUE_LEN         16

/*
 * Wire format. Every frame carries the sender's name and MAC so a frame on
 * its own identifies who sent it. Anything without our magic is dropped in
 * the receive callback (other teams use ESP-NOW on the same channel).
 */
#define FRAME_MAGIC   "AOT"
#define FRAME_VERSION 1
#define FLAG_BUSY     0x01  /* sender is connected to someone */

enum {
    T_HELLO = 1,  /* broadcast every second: here I am */
    T_JOIN,       /* unicast: connect with me? (resent every 300 ms) */
    T_JOIN_ACK,   /* unicast: yes */
    T_PING,       /* unicast: keepalive, also completes the handshake */
    T_DATA,       /* unicast: link_send() payload */
    T_BYE,        /* unicast: I'm hanging up */
};

typedef struct __attribute__((packed)) {
    uint8_t magic[3];
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    char name[LINK_NAME_LEN];
    uint8_t mac[6];
    uint8_t data[];
} frame_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t len;
    uint8_t buf[sizeof(frame_t) + LINK_MAX_DATA];
} rx_item_t;

typedef enum { ST_IDLE, ST_CONNECTING, ST_CONNECTED } state_t;
typedef enum { WANT_NONE, WANT_ANY, WANT_MAC } want_t;

/* Events are collected while holding the lock and delivered after it. */
typedef struct {
    int n;
    link_event_t ev[2];
    link_peer_t peer[2];
} events_t;

static const uint8_t BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static struct {
    SemaphoreHandle_t lock;
    QueueHandle_t rxq;
    link_cb_t cb;
    char name[LINK_NAME_LEN];
    uint8_t my_mac[6];

    want_t want;
    uint8_t want_mac[6];
    bool drop;              /* link_connect/link_disconnect asked to hang up */

    state_t state;
    bool initiator;         /* we sent JOIN (vs. answered one) */
    link_peer_t peer;       /* the board we are connecting/connected to */
    uint32_t state_since_ms;
    uint32_t last_rx_ms;
    uint32_t last_join_ms;
    uint32_t last_ping_ms;
    uint32_t next_hello_ms;
    uint32_t retry_at_ms;
    uint8_t skip_mac[6];    /* last board that didn't answer: try another one first */

    link_peer_t nearby[LINK_MAX_NEARBY];
} s;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool mac_eq(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

static void push_event(events_t *evs, link_event_t ev, const link_peer_t *peer)
{
    if (evs->n < 2) {
        evs->ev[evs->n] = ev;
        evs->peer[evs->n] = *peer;
        evs->n++;
    }
}

static esp_err_t send_frame(const uint8_t *dest, uint8_t type, const void *data, size_t len)
{
    uint8_t buf[sizeof(frame_t) + LINK_MAX_DATA];
    frame_t *f = (frame_t *)buf;

    memcpy(f->magic, FRAME_MAGIC, 3);
    f->version = FRAME_VERSION;
    f->type = type;
    f->flags = s.state == ST_CONNECTED ? FLAG_BUSY : 0;
    memcpy(f->name, s.name, LINK_NAME_LEN);
    memcpy(f->mac, s.my_mac, 6);
    if (len) {
        memcpy(f->data, data, len);
    }
    return esp_now_send(dest, buf, sizeof(frame_t) + len);
}

/* ESP-NOW needs a peer entry before unicasting. We keep at most one. */
static void use_peer(const uint8_t mac[6])
{
    if (!mac_eq(s.peer.mac, mac) && esp_now_is_peer_exist(s.peer.mac)) {
        esp_now_del_peer(s.peer.mac);
    }
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t p = {.ifidx = WIFI_IF_STA, .encrypt = false};
        memcpy(p.peer_addr, mac, 6);
        esp_now_add_peer(&p);
    }
}

static void nearby_update(const uint8_t mac[6], const char *name, int8_t rssi, bool busy)
{
    link_peer_t *slot = &s.nearby[0];
    for (int i = 0; i < LINK_MAX_NEARBY; i++) {
        if (mac_eq(s.nearby[i].mac, mac)) {
            slot = &s.nearby[i];
            break;
        }
        if (s.nearby[i].last_seen_ms < slot->last_seen_ms) {
            slot = &s.nearby[i];  /* oldest (or empty) so far */
        }
    }
    memcpy(slot->mac, mac, 6);
    memcpy(slot->name, name, LINK_NAME_LEN);
    slot->name[LINK_NAME_LEN] = '\0';
    slot->rssi = rssi;
    slot->busy = busy;
    slot->last_seen_ms = now_ms();
}

static void go_idle(events_t *evs)
{
    if (s.state == ST_CONNECTED) {
        ESP_LOGI(TAG, "disconnected from %s " MACSTR, s.peer.name, MAC2STR(s.peer.mac));
        push_event(evs, LINK_EVT_DISCONNECTED, &s.peer);
    }
    s.state = ST_IDLE;
    s.state_since_ms = now_ms();
    s.retry_at_ms = s.state_since_ms + esp_random() % RETRY_JITTER_MS;
}

static void start_connecting(const uint8_t mac[6], const char *name, bool initiator)
{
    use_peer(mac);
    memset(&s.peer, 0, sizeof(s.peer));
    memcpy(s.peer.mac, mac, 6);
    if (name) {
        memcpy(s.peer.name, name, LINK_NAME_LEN);
    }
    s.state = ST_CONNECTING;
    s.initiator = initiator;
    s.state_since_ms = now_ms();
    s.last_join_ms = 0;
}

static void become_connected(events_t *evs)
{
    s.state = ST_CONNECTED;
    s.last_rx_ms = now_ms();
    s.last_ping_ms = 0;  /* ping right away: it completes the peer's handshake */
    ESP_LOGI(TAG, "connected to %s " MACSTR " (%d dBm)", s.peer.name, MAC2STR(s.peer.mac), s.peer.rssi);
    push_event(evs, LINK_EVT_CONNECTED, &s.peer);
}

static bool accepts_join_from(const uint8_t mac[6])
{
    return s.want == WANT_NONE || s.want == WANT_ANY || mac_eq(s.want_mac, mac);
}

/* Returns true if the frame is DATA for the app (delivered after unlocking). */
static bool handle_rx(const rx_item_t *it, events_t *evs)
{
    const frame_t *f = (const frame_t *)it->buf;
    const bool from_peer = s.state != ST_IDLE && mac_eq(it->mac, s.peer.mac);

    nearby_update(it->mac, f->name, it->rssi, f->flags & FLAG_BUSY);
    /* Only frames sent to us count as keepalive: the peer keeps broadcasting
       HELLO even after it has dropped us. */
    if (from_peer && f->type != T_HELLO) {
        memcpy(s.peer.name, f->name, LINK_NAME_LEN);
        s.peer.rssi = it->rssi;
        s.peer.last_seen_ms = now_ms();
        s.last_rx_ms = now_ms();
    }

    switch (f->type) {
    case T_HELLO:
        if (s.state == ST_CONNECTED && from_peer && !(f->flags & FLAG_BUSY)) {
            ESP_LOGW(TAG, "%s says it's free: it dropped us", s.peer.name);
            go_idle(evs);  /* no need to wait for the 3 s timeout */
        }
        break;
    case T_JOIN:
        if (s.state == ST_CONNECTED) {
            if (!from_peer) {
                break;  /* busy */
            }
            go_idle(evs);  /* our peer restarted and is asking again */
        }
        if (!accepts_join_from(it->mac)) {
            break;
        }
        if (!(s.state == ST_CONNECTING && from_peer)) {
            start_connecting(it->mac, f->name, false);
        }
        send_frame(it->mac, T_JOIN_ACK, NULL, 0);
        break;
    case T_JOIN_ACK:
        if (s.state == ST_CONNECTING && from_peer) {
            become_connected(evs);
        }
        break;
    case T_PING:
        if (s.state == ST_CONNECTING && from_peer) {
            become_connected(evs);
        }
        break;
    case T_DATA:
        return s.state == ST_CONNECTED && from_peer && it->len > sizeof(frame_t);
    case T_BYE:
        if (from_peer) {
            if (s.state == ST_CONNECTED) {
                s.want = WANT_NONE;  /* they hung up: don't call them straight back */
            }
            go_idle(evs);
        }
        break;
    default:
        break;
    }
    return false;
}

static const link_peer_t *nearest_free(void)
{
    const link_peer_t *best = NULL;
    const uint32_t now = now_ms();
    for (int i = 0; i < LINK_MAX_NEARBY; i++) {
        const link_peer_t *p = &s.nearby[i];
        if (p->last_seen_ms && now - p->last_seen_ms < NEARBY_TIMEOUT_MS && !p->busy &&
            !mac_eq(p->mac, s.skip_mac) && (!best || p->rssi > best->rssi)) {
            best = p;
        }
    }
    return best;
}

static void tick(events_t *evs)
{
    const uint32_t now = now_ms();

    if ((int32_t)(now - s.next_hello_ms) >= 0) {
        send_frame(BROADCAST, T_HELLO, NULL, 0);
        s.next_hello_ms = now + HELLO_PERIOD_MS + esp_random() % HELLO_JITTER_MS;
    }

    if (s.drop) {
        s.drop = false;
        if (s.state != ST_IDLE) {
            send_frame(s.peer.mac, T_BYE, NULL, 0);
            go_idle(evs);
        }
    }

    switch (s.state) {
    case ST_CONNECTED:
        if (now - s.last_rx_ms > PEER_TIMEOUT_MS) {
            ESP_LOGW(TAG, "%s timed out", s.peer.name);
            go_idle(evs);
        } else if (!s.last_ping_ms || now - s.last_ping_ms >= PING_PERIOD_MS) {
            send_frame(s.peer.mac, T_PING, NULL, 0);
            s.last_ping_ms = now;
        }
        break;
    case ST_CONNECTING:
        if (now - s.state_since_ms > HANDSHAKE_TIMEOUT_MS) {
            if (s.initiator) {
                memcpy(s.skip_mac, s.peer.mac, 6);
            }
            go_idle(evs);
        } else if (s.initiator && (!s.last_join_ms || now - s.last_join_ms >= JOIN_RETRY_MS)) {
            send_frame(s.peer.mac, T_JOIN, NULL, 0);
            s.last_join_ms = now;
        }
        break;
    case ST_IDLE:
        if ((int32_t)(now - s.retry_at_ms) < 0) {
            break;
        }
        if (s.want == WANT_MAC) {
            start_connecting(s.want_mac, NULL, true);
        } else if (s.want == WANT_ANY) {
            const link_peer_t *p = nearest_free();
            memset(s.skip_mac, 0, 6);  /* skip it for one round only */
            if (p) {
                start_connecting(p->mac, p->name, true);
            }
        }
        break;
    }
}

static void link_task(void *arg)
{
    static rx_item_t it;  /* static: keeps 230 bytes off the task stack */

    while (1) {
        const bool got = xQueueReceive(s.rxq, &it, pdMS_TO_TICKS(TICK_MS)) == pdTRUE;
        events_t evs = {0};
        link_peer_t data_from;
        bool data = false;

        xSemaphoreTake(s.lock, portMAX_DELAY);
        if (got) {
            data = handle_rx(&it, &evs);
            data_from = s.peer;
        }
        tick(&evs);
        xSemaphoreGive(s.lock);

        if (!s.cb) {
            continue;
        }
        for (int i = 0; i < evs.n; i++) {
            s.cb(evs.ev[i], &evs.peer[i], NULL, 0);
        }
        if (data) {
            s.cb(LINK_EVT_DATA, &data_from, ((const frame_t *)it.buf)->data, it.len - sizeof(frame_t));
        }
    }
}

/* Runs in the Wi-Fi task: filter and copy, nothing else. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const frame_t *f = (const frame_t *)data;
    if (len < (int)sizeof(frame_t) || len > (int)(sizeof(frame_t) + LINK_MAX_DATA) ||
        memcmp(f->magic, FRAME_MAGIC, 3) != 0 || f->version != FRAME_VERSION) {
        return;
    }

    rx_item_t it;
    memcpy(it.mac, info->src_addr, 6);
    it.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    it.len = len;
    memcpy(it.buf, data, len);
    xQueueSend(s.rxq, &it, 0);
}

esp_err_t link_init(const char *name, link_cb_t cb)
{
    ESP_RETURN_ON_FALSE(!s.lock, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    bool ended = !name;
    for (int i = 0; i < LINK_NAME_LEN; i++) {
        ended = ended || name[i] == '\0';
        s.name[i] = ended ? '_' : (char)toupper((unsigned char)name[i]);
    }
    s.cb = cb;

    esp_err_t err = nvs_flash_init();
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
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi ps");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(CONFIG_LINK_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE), TAG, "channel");
    ESP_RETURN_ON_ERROR(esp_wifi_get_mac(WIFI_IF_STA, s.my_mac), TAG, "mac");

    s.lock = xSemaphoreCreateMutex();
    s.rxq = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_item_t));
    ESP_RETURN_ON_FALSE(s.lock && s.rxq, ESP_ERR_NO_MEM, TAG, "no mem");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "espnow init");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_recv), TAG, "recv cb");
    esp_now_peer_info_t bcast = {.ifidx = WIFI_IF_STA, .encrypt = false};
    memcpy(bcast.peer_addr, BROADCAST, 6);
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&bcast), TAG, "broadcast peer");

    ESP_RETURN_ON_FALSE(xTaskCreate(link_task, "link", 4096, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "task");
    ESP_LOGI(TAG, "%.3s " MACSTR " on channel %d", s.name, MAC2STR(s.my_mac), CONFIG_LINK_WIFI_CHANNEL);
    return ESP_OK;
}

esp_err_t link_connect(const uint8_t mac[6])
{
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (mac) {
        s.want = WANT_MAC;
        memcpy(s.want_mac, mac, 6);
        /* switching to a different board: hang up on the current one first */
        s.drop = s.state != ST_IDLE && !mac_eq(mac, s.peer.mac);
    } else {
        s.want = WANT_ANY;
    }
    s.retry_at_ms = now_ms();
    xSemaphoreGive(s.lock);
    return ESP_OK;
}

void link_disconnect(void)
{
    if (!s.lock) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.want = WANT_NONE;
    s.drop = s.state != ST_IDLE;
    xSemaphoreGive(s.lock);
}

bool link_get_peer(link_peer_t *out)
{
    if (!s.lock) {
        return false;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool connected = s.state == ST_CONNECTED;
    if (connected && out) {
        *out = s.peer;
    }
    xSemaphoreGive(s.lock);
    return connected;
}

esp_err_t link_send(const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(len <= LINK_MAX_DATA, ESP_ERR_INVALID_SIZE, TAG, "too long");
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    xSemaphoreTake(s.lock, portMAX_DELAY);
    esp_err_t err = s.state == ST_CONNECTED ? send_frame(s.peer.mac, T_DATA, data, len) : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(s.lock);
    return err;
}

int link_nearby(link_peer_t *out, int max)
{
    if (!s.lock) {
        return 0;
    }
    link_peer_t sorted[LINK_MAX_NEARBY];
    int n = 0;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const uint32_t now = now_ms();
    for (int i = 0; i < LINK_MAX_NEARBY; i++) {
        const link_peer_t *p = &s.nearby[i];
        if (!p->last_seen_ms || now - p->last_seen_ms >= NEARBY_TIMEOUT_MS) {
            continue;
        }
        int j = n++;  /* insertion sort, strongest first */
        while (j > 0 && sorted[j - 1].rssi < p->rssi) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = *p;
    }
    xSemaphoreGive(s.lock);
    n = n < max ? n : max;
    memcpy(out, sorted, n * sizeof(link_peer_t));
    return n;
}

void link_get_mac(uint8_t mac[6])
{
    esp_wifi_get_mac(WIFI_IF_STA, mac);
}

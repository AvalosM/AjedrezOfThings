/*
 * The link state machine. See link.h for the states and what moves between
 * them. Everything runs in one task (link_task): it handles each received
 * frame, then calls tick() to send what's due and check timeouts.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "link.h"
#include "presence.h"
#include "radio.h"

static const char *TAG = "link";

#define INVITE_RETRY_MS    300      /* resend INVITE until we get an answer */
#define INVITE_TIMEOUT_MS  15000    /* time for the other person to press a key */
#define PING_PERIOD_MS     1000
#define SILENCE_TIMEOUT_MS 3000     /* nothing from the peer for this long: it's gone */
#define AFTER_BYE_MS       1000     /* ignore invites from the board we just left */
#define TICK_MS            50
#define RX_QUEUE_LEN       16

/* Events are collected while holding the lock and delivered after
 * releasing it, so the callback can call link functions. At most two per
 * loop: one from a received frame, one from tick(). */
typedef struct {
    link_event_t ev;
    link_peer_t peer;
    const uint8_t *data;
    size_t len;
} pending_t;

typedef struct {
    int n;
    pending_t list[2];
} events_t;

static struct {
    SemaphoreHandle_t lock;         /* link task vs. the app calling link_*() */
    QueueHandle_t rxq;              /* received frames, filled by radio */
    link_cb_t cb;
    link_state_t state;
    link_peer_t peer;               /* who we invite, were invited by, or are connected to */
    uint32_t state_since_ms;
    uint32_t last_heard_ms;         /* last frame from the peer (HELLO doesn't count) */
    uint32_t last_sent_ms;          /* last INVITE or PING we sent */
    uint8_t left_mac[6];            /* the board we were last with... */
    uint32_t left_ms;               /* ...and when we went back to IDLE */
} s;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void add_event(events_t *evs, link_event_t ev, const uint8_t *data, size_t len)
{
    if (evs && evs->n < 2) {
        evs->list[evs->n++] = (pending_t){ev, s.peer, data, len};
    }
}

/* Every state change goes through here. evs == NULL means the app asked for
 * it (link_invite/accept/hangup), so there's no event to report. */
static void set_state(link_state_t state, events_t *evs)
{
    static const char *const NAMES[] = {"IDLE", "INVITING", "INVITED", "CONNECTED"};
    ESP_LOGI(TAG, "%s -> %s (%s)", NAMES[s.state], NAMES[state], s.peer.name);

    if (state == LINK_INVITED) {
        add_event(evs, LINK_EVT_INVITED, NULL, 0);
    } else if (state == LINK_CONNECTED) {
        add_event(evs, LINK_EVT_CONNECTED, NULL, 0);
    } else if (state == LINK_IDLE) {
        add_event(evs, LINK_EVT_DISCONNECTED, NULL, 0);
        memcpy(s.left_mac, s.peer.mac, 6);
        s.left_ms = now_ms();
    }

    s.state = state;
    s.state_since_ms = now_ms();
    s.last_heard_ms = s.state_since_ms;
    s.last_sent_ms = 0;  /* send the first INVITE/PING right away */
}

static void set_peer(const uint8_t mac[6], const char *name, int8_t rssi)
{
    radio_set_peer(mac);
    memset(&s.peer, 0, sizeof(s.peer));
    memcpy(s.peer.mac, mac, 6);
    memcpy(s.peer.name, name, LINK_NAME_LEN);
    s.peer.rssi = rssi;
}

static esp_err_t send_to_peer(frame_type_t type, const void *data, size_t len)
{
    return radio_send(s.peer.mac, type, s.state != LINK_IDLE, data, len);
}

static void handle_frame(const frame_rx_t *rx, events_t *evs)
{
    const frame_t *f = (const frame_t *)rx->buf;
    const bool from_peer = s.state != LINK_IDLE && memcmp(rx->mac, s.peer.mac, 6) == 0;

    if (f->type == FRAME_HELLO) {
        presence_heard(rx->mac, f->name, rx->rssi, f->flags & FLAG_BUSY, now_ms());
        return;
    }
    if (from_peer) {
        s.peer.rssi = rx->rssi;
        s.last_heard_ms = now_ms();
    }

    switch (f->type) {
    case FRAME_INVITE:
        if (s.state == LINK_IDLE && memcmp(rx->mac, s.left_mac, 6) == 0 && now_ms() - s.left_ms < AFTER_BYE_MS) {
            break;  /* a late copy of an invite we just declined or cancelled */
        }
        if (s.state == LINK_IDLE) {
            set_peer(rx->mac, f->name, rx->rssi);
            set_state(LINK_INVITED, evs);
        } else if (from_peer && s.state == LINK_INVITING) {
            /* We invited each other: that's a yes from both. */
            send_to_peer(FRAME_ACCEPT, NULL, 0);
            set_state(LINK_CONNECTED, evs);
        } else if (from_peer && s.state == LINK_CONNECTED) {
            /* Our ACCEPT got lost and they're still asking. */
            send_to_peer(FRAME_ACCEPT, NULL, 0);
        }
        /* Anyone else while we're busy: ignore. Their invite expires. */
        break;
    case FRAME_ACCEPT:
        if (from_peer && s.state == LINK_INVITING) {
            set_state(LINK_CONNECTED, evs);
        }
        break;
    case FRAME_DATA:
        if (from_peer && s.state == LINK_CONNECTED) {
            add_event(evs, LINK_EVT_DATA, f->data, rx->len - sizeof(frame_t));
        }
        break;
    case FRAME_BYE:
        if (from_peer) {
            set_state(LINK_IDLE, evs);
        }
        break;
    default:
        break;  /* PING: it only keeps the link alive, done above */
    }
}

static void tick(events_t *evs)
{
    const uint32_t now = now_ms();

    if (presence_hello_due(now)) {
        radio_send(RADIO_BROADCAST, FRAME_HELLO, s.state != LINK_IDLE, NULL, 0);
    }

    switch (s.state) {
    case LINK_IDLE:
        break;
    case LINK_INVITING:
        if (now - s.state_since_ms > INVITE_TIMEOUT_MS) {
            set_state(LINK_IDLE, evs);  /* nobody answered */
        } else if (now - s.last_sent_ms >= INVITE_RETRY_MS) {
            send_to_peer(FRAME_INVITE, NULL, 0);
            s.last_sent_ms = now;
        }
        break;
    case LINK_INVITED:
        if (now - s.last_heard_ms > SILENCE_TIMEOUT_MS) {
            set_state(LINK_IDLE, evs);  /* the inviter stopped asking */
        }
        break;
    case LINK_CONNECTED:
        if (now - s.last_heard_ms > SILENCE_TIMEOUT_MS) {
            set_state(LINK_IDLE, evs);  /* the peer went away */
        } else if (now - s.last_sent_ms >= PING_PERIOD_MS) {
            send_to_peer(FRAME_PING, NULL, 0);
            s.last_sent_ms = now;
        }
        break;
    }
}

static void link_task(void *arg)
{
    /* static: keeps it off the stack, and DATA events point into it until
       they've been delivered */
    static frame_rx_t rx;

    while (1) {
        const bool got = xQueueReceive(s.rxq, &rx, pdMS_TO_TICKS(TICK_MS)) == pdTRUE;
        events_t evs = {0};

        xSemaphoreTake(s.lock, portMAX_DELAY);
        if (got) {
            handle_frame(&rx, &evs);
        }
        tick(&evs);
        xSemaphoreGive(s.lock);

        for (int i = 0; i < evs.n && s.cb; i++) {
            const pending_t *e = &evs.list[i];
            s.cb(e->ev, &e->peer, e->data, e->len);
        }
    }
}

esp_err_t link_init(const char *name, link_cb_t cb)
{
    s.cb = cb;
    s.lock = xSemaphoreCreateMutex();
    s.rxq = xQueueCreate(RX_QUEUE_LEN, sizeof(frame_rx_t));
    ESP_RETURN_ON_FALSE(s.lock && s.rxq, ESP_ERR_NO_MEM, TAG, "no memory");
    ESP_RETURN_ON_ERROR(radio_init(name, s.rxq), TAG, "radio");
    ESP_RETURN_ON_FALSE(xTaskCreate(link_task, "link", 4096, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "no memory for the task");

    ESP_LOGI(TAG, "I am %.3s, on channel %d", name, CONFIG_LINK_WIFI_CHANNEL);
    return ESP_OK;
}

int link_nearby(link_peer_t *out, int max)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const int n = presence_nearby(out, max, now_ms());
    xSemaphoreGive(s.lock);
    return n;
}

esp_err_t link_invite(const link_peer_t *who)
{
    esp_err_t err = ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.state == LINK_IDLE) {
        set_peer(who->mac, who->name, who->rssi);
        set_state(LINK_INVITING, NULL);
        err = ESP_OK;
    }
    xSemaphoreGive(s.lock);
    return err;
}

esp_err_t link_accept(void)
{
    esp_err_t err = ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.state == LINK_INVITED) {
        set_state(LINK_CONNECTED, NULL);
        send_to_peer(FRAME_ACCEPT, NULL, 0);
        err = ESP_OK;
    }
    xSemaphoreGive(s.lock);
    return err;
}

void link_hangup(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.state != LINK_IDLE) {
        /* Sent once. If it gets lost, the peer notices the silence after 3 s. */
        send_to_peer(FRAME_BYE, NULL, 0);
        set_state(LINK_IDLE, NULL);
    }
    xSemaphoreGive(s.lock);
}

link_state_t link_get_state(link_peer_t *peer)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const link_state_t state = s.state;
    if (peer && state != LINK_IDLE) {
        *peer = s.peer;
    }
    xSemaphoreGive(s.lock);
    return state;
}

esp_err_t link_send(const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(len <= LINK_MAX_DATA, ESP_ERR_INVALID_SIZE, TAG, "too long");
    esp_err_t err = ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.state == LINK_CONNECTED) {
        err = send_to_peer(FRAME_DATA, data, len);
    }
    xSemaphoreGive(s.lock);
    return err;
}

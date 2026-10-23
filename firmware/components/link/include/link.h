#pragma once

/*
 * link: pair two kiwi boards over ESP-NOW and keep the connection alive.
 *
 * Proof of concept. What it does:
 *   - every board broadcasts HELLO {name, mac} once a second, so boards can
 *     list who is nearby (link_nearby);
 *   - link_connect() runs a 3-way handshake (JOIN -> JOIN_ACK -> PING) with
 *     one other board;
 *   - once connected, both sides send PING every second; 3 s without hearing
 *     the peer means the connection is lost, and link keeps trying to
 *     reconnect to the board you asked for;
 *   - link_send() sends up to LINK_MAX_DATA bytes to the peer, best-effort
 *     (ESP-NOW's own retries only, no ACK/ordering yet).
 *
 * Minimal use:
 *
 *     static void on_link(link_event_t ev, const link_peer_t *peer,
 *                         const uint8_t *data, size_t len)
 *     {
 *         if (ev == LINK_EVT_CONNECTED) printf("hi %s\n", peer->name);
 *     }
 *
 *     link_init("AVM", on_link);
 *     link_connect(NULL);          // pair with the nearest free board
 *
 * Threading: the callback runs in link's own task. Keep it short (copy the
 * data, post to your queue). All functions may be called from any task.
 *
 * Wire format (see link.c, and tools/espnow_probe.py for a laptop peer):
 *   'A' 'O' 'T' | version 1 | type | flags | name[3] | mac[6] | data...
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define LINK_NAME_LEN   3       /* arcade style: exactly 3 characters */
#define LINK_MAX_DATA   200     /* max bytes per link_send() */
#define LINK_MAX_NEARBY 16      /* boards remembered by link_nearby() */

typedef struct {
    uint8_t mac[6];
    char name[LINK_NAME_LEN + 1];   /* NUL-terminated */
    int8_t rssi;                    /* dBm of the last frame heard from it */
    bool busy;                      /* already connected to someone */
    uint32_t last_seen_ms;          /* ms since boot */
} link_peer_t;

typedef enum {
    LINK_EVT_CONNECTED,     /* peer = the board we are now connected to */
    LINK_EVT_DISCONNECTED,  /* peer = the board we lost (timeout or BYE) */
    LINK_EVT_DATA,          /* peer sent data/len with link_send() */
} link_event_t;

typedef void (*link_cb_t)(link_event_t ev, const link_peer_t *peer, const uint8_t *data, size_t len);

/*
 * Start NVS, Wi-Fi (station, no AP, channel CONFIG_LINK_WIFI_CHANNEL),
 * ESP-NOW and the link task. name: 3 chars, shorter is padded with '_'.
 * cb may be NULL. After init the board answers incoming connections but
 * does not start one: call link_connect() for that.
 */
esp_err_t link_init(const char *name, link_cb_t cb);

/*
 * Ask to connect. mac = a board from link_nearby(), or NULL for "the
 * nearest free board". Returns at once; LINK_EVT_CONNECTED tells you when it
 * worked. link keeps retrying, and reconnects after a loss, until
 * link_disconnect().
 */
esp_err_t link_connect(const uint8_t mac[6]);

/* Drop the connection (sends BYE) and stop retrying. Incoming connections
 * are still accepted. */
void link_disconnect(void);

/* true and *out filled if connected. out may be NULL. */
bool link_get_peer(link_peer_t *out);

/* Send to the connected peer. ESP_ERR_INVALID_STATE if not connected,
 * ESP_ERR_INVALID_SIZE if len > LINK_MAX_DATA. */
esp_err_t link_send(const void *data, size_t len);

/* Copy boards heard in the last 3 s, strongest first. Returns the count. */
int link_nearby(link_peer_t *out, int max);

/* This board's Wi-Fi station MAC. */
void link_get_mac(uint8_t mac[6]);

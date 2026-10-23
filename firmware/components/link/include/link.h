#pragma once

/*
 * link: who is nearby, and a connection between two boards, over ESP-NOW.
 *
 * Presence: every board broadcasts HELLO {name, busy} once a second.
 *   link_nearby() lists the boards heard in the last 3 s.
 *
 * Link: a connection with exactly one other board (the peer), like a call:
 *
 *     IDLE --link_invite()------> INVITING --peer accepts----> CONNECTED
 *     IDLE --an INVITE arrives--> INVITED  --link_accept()---> CONNECTED
 *     any state --link_hangup(), the peer declines/hangs up,
 *                 or 3 s of silence-------------------------> IDLE
 *
 *   While connected both sides PING every second. link_send() sends to the
 *   peer, best effort: no ACK, no ordering (add those on top if you need them).
 *
 * Events: the callback reports what the OTHER board or a timeout did. Your
 * own calls (invite, accept, hangup) change the state but don't produce
 * events. The callback runs in link's own task: keep it short (copy and
 * post to a queue).
 *
 * Call link_init() before anything else.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define LINK_NAME_LEN 3     /* arcade style: exactly 3 characters */
#define LINK_MAX_DATA 200   /* max bytes per link_send() */

typedef struct {
    uint8_t mac[6];
    char name[LINK_NAME_LEN + 1];   /* NUL-terminated */
    int8_t rssi;                    /* signal strength (dBm) of the last frame heard */
    bool busy;                      /* already inviting, invited or connected */
} link_peer_t;

typedef enum {
    LINK_IDLE,          /* not connected: the lobby */
    LINK_INVITING,      /* we invited the peer, waiting for an answer */
    LINK_INVITED,       /* the peer invited us: link_accept() or link_hangup() */
    LINK_CONNECTED,
} link_state_t;

typedef enum {
    LINK_EVT_INVITED,       /* peer invited us */
    LINK_EVT_CONNECTED,     /* peer accepted our invite */
    LINK_EVT_DISCONNECTED,  /* peer declined/hung up, our invite expired, or the peer went silent */
    LINK_EVT_DATA,          /* peer sent data/len with link_send() */
} link_event_t;

typedef void (*link_cb_t)(link_event_t ev, const link_peer_t *peer, const uint8_t *data, size_t len);

/* Start Wi-Fi + ESP-NOW and the link task. name: exactly LINK_NAME_LEN
 * characters, sent in every frame. cb may be NULL. */
esp_err_t link_init(const char *name, link_cb_t cb);

/* Boards heard in the last 3 s, sorted by tag (then MAC, if two share a tag).
 * Returns the count. */
int link_nearby(link_peer_t *out, int max);

/* IDLE -> INVITING. who: a board from link_nearby(). Keeps asking for 15 s.
 * ESP_ERR_INVALID_STATE if we're not idle. */
esp_err_t link_invite(const link_peer_t *who);

/* INVITED -> CONNECTED. ESP_ERR_INVALID_STATE if nobody is inviting us. */
esp_err_t link_accept(void);

/* Any state -> IDLE: cancels our invite, declines theirs, or hangs up. */
void link_hangup(void);

/* Current state. If peer isn't NULL and we're not idle, it gets the peer. */
link_state_t link_get_state(link_peer_t *peer);

/* Send to the connected peer. ESP_ERR_INVALID_STATE if not connected,
 * ESP_ERR_INVALID_SIZE if len > LINK_MAX_DATA. */
esp_err_t link_send(const void *data, size_t len);

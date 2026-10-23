#pragma once

/*
 * presence: when to broadcast HELLO, and the list of boards we heard.
 * Not thread-safe: link.c only calls these while holding its lock.
 */

#include <stdbool.h>
#include <stdint.h>

#include "link.h"

/* true once a second (plus a little random delay): time to send HELLO. */
bool presence_hello_due(uint32_t now_ms);

/* Remember a HELLO from mac. */
void presence_heard(const uint8_t mac[6], const char *name, int8_t rssi, bool busy, uint32_t now_ms);

/* Boards heard in the last 3 s, sorted by tag. Returns the count. */
int presence_nearby(link_peer_t *out, int max, uint32_t now_ms);

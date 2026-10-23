#include <string.h>

#include "esp_random.h"

#include "presence.h"

#define HELLO_PERIOD_MS   1000
#define HELLO_JITTER_MS   200   /* so 20 boards don't all talk at the same instant */
#define NEARBY_TIMEOUT_MS 3000  /* forget a board after this long without a HELLO */
#define MAX_REMEMBERED    16

typedef struct {
    link_peer_t peer;
    uint32_t heard_ms;  /* 0 = empty slot */
} entry_t;

static entry_t s_table[MAX_REMEMBERED];
static uint32_t s_next_hello_ms;

bool presence_hello_due(uint32_t now_ms)
{
    if ((int32_t)(now_ms - s_next_hello_ms) < 0) {
        return false;
    }
    s_next_hello_ms = now_ms + HELLO_PERIOD_MS + esp_random() % HELLO_JITTER_MS;
    return true;
}

void presence_heard(const uint8_t mac[6], const char *name, int8_t rssi, bool busy, uint32_t now_ms)
{
    /* Its own slot if we know it, otherwise the oldest (or an empty) one. */
    entry_t *slot = &s_table[0];
    for (int i = 0; i < MAX_REMEMBERED; i++) {
        if (memcmp(s_table[i].peer.mac, mac, 6) == 0) {
            slot = &s_table[i];
            break;
        }
        if (s_table[i].heard_ms < slot->heard_ms) {
            slot = &s_table[i];
        }
    }

    memcpy(slot->peer.mac, mac, 6);
    memcpy(slot->peer.name, name, LINK_NAME_LEN);
    slot->peer.name[LINK_NAME_LEN] = '\0';
    slot->peer.rssi = rssi;
    slot->peer.busy = busy;
    slot->heard_ms = now_ms;
}

int presence_nearby(link_peer_t *out, int max, uint32_t now_ms)
{
    link_peer_t fresh[MAX_REMEMBERED];
    int n = 0;
    for (int i = 0; i < MAX_REMEMBERED; i++) {
        if (s_table[i].heard_ms && now_ms - s_table[i].heard_ms < NEARBY_TIMEOUT_MS) {
            fresh[n++] = s_table[i].peer;
        }
    }

    /* Strongest signal first. Insertion sort: there are at most 16. */
    for (int i = 1; i < n; i++) {
        link_peer_t p = fresh[i];
        int j = i;
        while (j > 0 && fresh[j - 1].rssi < p.rssi) {
            fresh[j] = fresh[j - 1];
            j--;
        }
        fresh[j] = p;
    }

    if (n > max) {
        n = max;
    }
    memcpy(out, fresh, n * sizeof(link_peer_t));
    return n;
}

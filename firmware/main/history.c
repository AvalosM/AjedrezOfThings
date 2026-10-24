#include "history.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define NVS_HIST_NS "match_hist"
#define MAX_HISTORY_ENTRIES 32

typedef struct {
    uint8_t mac[6];
    match_history_t hist;
    bool valid;
} history_entry_t;

static history_entry_t s_history_cache[MAX_HISTORY_ENTRIES];

static void mac_to_key(const uint8_t mac[6], char key[15])
{
    snprintf(key, 15, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void history_get(const uint8_t mac[6], match_history_t *out)
{
    for (int i = 0; i < MAX_HISTORY_ENTRIES; i++) {
        if (s_history_cache[i].valid && memcmp(s_history_cache[i].mac, mac, 6) == 0) {
            *out = s_history_cache[i].hist;
            return;
        }
    }

    out->wins = 0;
    out->losses = 0;

    char key[15];
    mac_to_key(mac, key);

    nvs_handle_t nvs;
    if (nvs_open(NVS_HIST_NS, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t val = 0;
        if (nvs_get_u32(nvs, key, &val) == ESP_OK) {
            out->wins = (uint16_t)((val >> 16) & 0xFFFF);
            out->losses = (uint16_t)(val & 0xFFFF);
        }
        nvs_close(nvs);
    }

    int insert_idx = -1;
    for (int i = 0; i < MAX_HISTORY_ENTRIES; i++) {
        if (!s_history_cache[i].valid) {
            insert_idx = i;
            break;
        }
    }
    if (insert_idx == -1) {
        insert_idx = 0;
    }

    memcpy(s_history_cache[insert_idx].mac, mac, 6);
    s_history_cache[insert_idx].hist = *out;
    s_history_cache[insert_idx].valid = true;
}

void history_record(const uint8_t mac[6], bool won)
{
    match_history_t hist;
    history_get(mac, &hist);

    if (won) {
        if (hist.wins < 0xFFFF) {
            hist.wins++;
        }
    } else {
        if (hist.losses < 0xFFFF) {
            hist.losses++;
        }
    }

    for (int i = 0; i < MAX_HISTORY_ENTRIES; i++) {
        if (s_history_cache[i].valid && memcmp(s_history_cache[i].mac, mac, 6) == 0) {
            s_history_cache[i].hist = hist;
            break;
        }
    }

    char key[15];
    mac_to_key(mac, key);
    uint32_t val = ((uint32_t)hist.wins << 16) | (hist.losses & 0xFFFF);

    nvs_handle_t nvs;
    if (nvs_open(NVS_HIST_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, key, val);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

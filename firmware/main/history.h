#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t wins;
    uint16_t losses;
} match_history_t;

void history_get(const uint8_t mac[6], match_history_t *out);
void history_record(const uint8_t mac[6], bool won);

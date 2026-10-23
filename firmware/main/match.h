#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    COLOR_WHITE = 0, 
    COLOR_BLACK = 1 
} player_color_t;

typedef struct {
    bool active;

    player_color_t my_color;          

    bool is_my_turn;
    uint8_t rival_mac[6];
    char rival_name[4];
    uint8_t turn_count;
    //board 
} match_t;

extern match_t current_match;

void startMatch(player_color_t color, const uint8_t *rival_mac, const char *rival_name, uint32_t seed);
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    COLOR_WHITE = 0, 
    COLOR_BLACK = 1 
} player_color_t;

typedef struct { 
} board_t;

typedef struct {
    bool active;
    
    player_color_t my_color;          

    bool is_my_turn;
    uint8_t rival_mac[6];
    char rival_name[4];
    uint32_t seed;
    uint8_t turn_count;
    char winner_name[4];
    int32_t my_time_secs;
    int32_t rival_time_secs;
    uint32_t seed;
    board_t board; 
} match_t;

typedef struct {
    uint8_t turn_count;
    board_t board;
    bool match_finished;
    char winner_name[4];
} turn_result_t;

extern match_t current_match;

void startMatch(player_color_t color, const uint8_t *rival_mac, const char *rival_name, uint8_t duration_minutes, uint32_t seed);

bool makeMove(turn_result_t *out_result);

bool receiveMove(const turn_result_t *move);
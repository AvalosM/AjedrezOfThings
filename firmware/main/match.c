#include "match.h"

match_t current_match = {0};

void startMatch(player_color_t color, const uint8_t *rival_mac, const char *rival_name, uint32_t seed) {
    memset(&current_match, 0, sizeof(match_t));
    current_match.active = true;
    current_match.my_color = color;
    current_match.is_my_turn = (color == COLOR_WHITE);
    current_match.seed = seed;
    current_match.turn_count = 0;
    memcpy(current_match.rival_mac, rival_mac, 6);
    strncpy(current_match.rival_name, rival_name, sizeof(current_match.rival_name) - 1);

    // inicializar board con la seed
}
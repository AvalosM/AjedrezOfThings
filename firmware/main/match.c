#include "match.h"
#include <string.h>

match_t current_match = {0};

void startMatch(player_color_t color, const uint8_t *rival_mac, const char *rival_name, uint8_t duration_minutes, uint32_t seed) {
    memset(&current_match, 0, sizeof(match_t));
    current_match.active = true;
    current_match.my_color = color;
    current_match.is_my_turn = (color == COLOR_WHITE);
    current_match.seed = seed;
    current_match.turn_count = 0;
    current_match.my_time_secs = duration_minutes * 60;
    current_match.rival_time_secs = duration_minutes * 60;
    memcpy(current_match.rival_mac, rival_mac, 6);
    
    strncpy(current_match.rival_name, rival_name, sizeof(current_match.rival_name) - 1);
    current_match.rival_name[sizeof(current_match.rival_name) - 1] = '\0';

    // TODO: inicializar board con la seed
}

bool makeMove(turn_result_t *out_result) {
    if (!current_match.active) {
        return false;
    }

    if (!current_match.is_my_turn) {
        return false;
    }

    // TODO: actualizar board, logica de ganar

    current_match.turn_count += 1;
    current_match.is_my_turn = false;

    out_result->turn_count = current_match.turn_count;
    out_result->board = current_match.board;
    out_result->match_finished = !current_match.active;
    strncpy(out_result->winner_name, current_match.winner_name, sizeof(out_result->winner_name) - 1);
    out_result->winner_name[sizeof(out_result->winner_name) - 1] = '\0';

    return true;
}

bool receiveMove(const turn_result_t *move) {
    if (!current_match.active) {
        return false;
    }

    if (current_match.is_my_turn) {
        return false;
    }

    if (move->turn_count <= current_match.turn_count) {
        return false;
    }

    current_match.turn_count = move->turn_count;
    current_match.board = move->board;

    if (move->match_finished) {
        current_match.active = false;
        strncpy(current_match.winner_name, move->winner_name, sizeof(current_match.winner_name) - 1);
        current_match.winner_name[sizeof(current_match.winner_name) - 1] = '\0';
    } else {
        current_match.is_my_turn = true;
    }

    return true;
}

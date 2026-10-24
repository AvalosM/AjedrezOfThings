#pragma once

#include <stdbool.h>
#include "chessboard.h"

typedef enum {
    AI_EASY = 0,
    AI_MEDIUM,
    AI_HARD,
    AI_DIFFICULTY_COUNT
} ai_difficulty_t;

const char *ai_difficulty_name(ai_difficulty_t diff);

/*
 * Finds the best move for ai_is_white in the current board.
 * Returns true if a legal move was found, false if there are no legal moves.
 */
bool ai_find_best_move(const board_t *board, bool ai_is_white, ai_difficulty_t difficulty,
                       int *out_x0, int *out_y0, int *out_x1, int *out_y1);

#include "ai.h"

#include <stdlib.h>
#include <string.h>

#include "esp_random.h"

#define MAX_LEGAL_MOVES 96
#define SCORE_WIN       100000
#define SCORE_LOSS     -100000
#define SCORE_STALEMATE -50000

typedef struct {
    uint8_t x0, y0;
    uint8_t x1, y1;
    int score;
} ai_candidate_move_t;

static const int s_piece_values[] = {
    [PIECE_BISHOP] = 320,
    [PIECE_KNIGHT] = 300,
    [PIECE_PAWN]   = 100,
    [PIECE_ROOK]   = 500,
    [PIECE_KING]   = 400,
    [PIECE_QUEEN]  = 900,
    [PIECE_EMPTY]  = 0,
};

const char *ai_difficulty_name(ai_difficulty_t diff)
{
    switch (diff) {
    case AI_EASY:   return "FACIL";
    case AI_MEDIUM: return "MEDIO";
    case AI_HARD:   return "DIFICIL";
    default:        return "NORMAL";
    }
}

static int generate_legal_moves(const board_t *board, bool is_white, ai_candidate_move_t *out_moves, int max_moves)
{
    int count = 0;
    const player_t player = {.is_white = is_white};

    for (int y0 = 0; y0 < 5; y0++) {
        for (int x0 = 0; x0 < 5; x0++) {
            const piece_t p = board->sq[y0][x0];
            if (p.piece == PIECE_EMPTY || p.is_white != is_white) {
                continue;
            }

            for (int y1 = 0; y1 < 5; y1++) {
                for (int x1 = 0; x1 < 5; x1++) {
                    if (x0 == x1 && y0 == y1) {
                        continue;
                    }
                    board_t temp = *board;
                    if (board_move_and_capture(x0, y0, x1, y1, temp.sq, player)) {
                        if (count < max_moves) {
                            out_moves[count] = (ai_candidate_move_t){
                                .x0 = (uint8_t)x0,
                                .y0 = (uint8_t)y0,
                                .x1 = (uint8_t)x1,
                                .y1 = (uint8_t)y1,
                                .score = 0,
                            };
                            count++;
                        }
                    }
                }
            }
        }
    }
    return count;
}

static int evaluate_board(const board_t *board, bool ai_is_white)
{
    int white_count = board_count_pieces(board->sq, true);
    int black_count = board_count_pieces(board->sq, false);

    if (white_count == 0) {
        return ai_is_white ? SCORE_LOSS : SCORE_WIN;
    }
    if (black_count == 0) {
        return ai_is_white ? SCORE_WIN : SCORE_LOSS;
    }

    int white_material = 0;
    int black_material = 0;

    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            const piece_t p = board->sq[y][x];
            if (p.piece == PIECE_EMPTY) {
                continue;
            }

            int val = s_piece_values[p.piece];

            // Positional bonus: center control
            if (x == 2 && y == 2) {
                val += 15;
            } else if (x >= 1 && x <= 3 && y >= 1 && y <= 3) {
                val += 5;
            }

            // Pawn advancement bonus
            if (p.piece == PIECE_PAWN) {
                if (p.is_white) {
                    // White advances toward y=4
                    if (y == 1) val += 10;
                    else if (y == 2) val += 25;
                    else if (y == 3) val += 60;
                } else {
                    // Black advances toward y=0
                    if (y == 3) val += 10;
                    else if (y == 2) val += 25;
                    else if (y == 1) val += 60;
                }
            }

            if (p.is_white) {
                white_material += val;
            } else {
                black_material += val;
            }
        }
    }

    int score = white_material - black_material;
    return ai_is_white ? score : -score;
}

static int minimax(const board_t *board, int depth, int alpha, int beta, bool current_player_white, bool ai_is_white)
{
    int white_count = board_count_pieces(board->sq, true);
    int black_count = board_count_pieces(board->sq, false);

    if (white_count == 0 || black_count == 0 || depth <= 0) {
        return evaluate_board(board, ai_is_white);
    }

    ai_candidate_move_t moves[MAX_LEGAL_MOVES];
    int n = generate_legal_moves(board, current_player_white, moves, MAX_LEGAL_MOVES);

    if (n == 0) {
        // Player whose turn it is has no moves left
        return (current_player_white == ai_is_white) ? SCORE_STALEMATE : -SCORE_STALEMATE;
    }

    bool is_maximizing = (current_player_white == ai_is_white);

    if (is_maximizing) {
        int max_eval = -999999;
        for (int i = 0; i < n; i++) {
            board_t next_board = *board;
            player_t p = {.is_white = current_player_white};
            board_move_and_capture(moves[i].x0, moves[i].y0, moves[i].x1, moves[i].y1, next_board.sq, p);

            int eval = minimax(&next_board, depth - 1, alpha, beta, !current_player_white, ai_is_white);
            if (eval > max_eval) {
                max_eval = eval;
            }
            if (eval > alpha) {
                alpha = eval;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return max_eval;
    } else {
        int min_eval = 999999;
        for (int i = 0; i < n; i++) {
            board_t next_board = *board;
            player_t p = {.is_white = current_player_white};
            board_move_and_capture(moves[i].x0, moves[i].y0, moves[i].x1, moves[i].y1, next_board.sq, p);

            int eval = minimax(&next_board, depth - 1, alpha, beta, !current_player_white, ai_is_white);
            if (eval < min_eval) {
                min_eval = eval;
            }
            if (eval < beta) {
                beta = eval;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return min_eval;
    }
}

bool ai_find_best_move(const board_t *board, bool ai_is_white, ai_difficulty_t difficulty,
                       int *out_x0, int *out_y0, int *out_x1, int *out_y1)
{
    ai_candidate_move_t moves[MAX_LEGAL_MOVES];
    int n = generate_legal_moves(board, ai_is_white, moves, MAX_LEGAL_MOVES);
    if (n == 0) {
        return false;
    }

    int depth = 1;
    if (difficulty == AI_MEDIUM) {
        depth = 2;
    } else if (difficulty == AI_HARD) {
        depth = 4;
    }

    // In Easy mode: 35% chance to make a random legal move for casual play
    if (difficulty == AI_EASY && (esp_random() % 100) < 35) {
        int idx = esp_random() % n;
        *out_x0 = moves[idx].x0;
        *out_y0 = moves[idx].y0;
        *out_x1 = moves[idx].x1;
        *out_y1 = moves[idx].y1;
        return true;
    }

    int best_score = -999999;
    int best_indices[MAX_LEGAL_MOVES];
    int best_count = 0;

    for (int i = 0; i < n; i++) {
        board_t next_board = *board;
        player_t p = {.is_white = ai_is_white};
        board_move_and_capture(moves[i].x0, moves[i].y0, moves[i].x1, moves[i].y1, next_board.sq, p);

        int score = minimax(&next_board, depth - 1, -999999, 999999, !ai_is_white, ai_is_white);

        if (score > best_score) {
            best_score = score;
            best_indices[0] = i;
            best_count = 1;
        } else if (score == best_score) {
            best_indices[best_count++] = i;
        }
    }

    int chosen_idx = best_indices[esp_random() % best_count];
    *out_x0 = moves[chosen_idx].x0;
    *out_y0 = moves[chosen_idx].y0;
    *out_x1 = moves[chosen_idx].x1;
    *out_y1 = moves[chosen_idx].y1;
    return true;
}

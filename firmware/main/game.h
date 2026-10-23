#pragma once

/*
 * game: a match as played on this board: the pieces, our color, the cursor
 * the joystick moves, and keeping the other board in sync.
 *
 * After each of our moves we send the whole board to the other board (MOVE)
 * and resend it every 300 ms until it answers (ACK). See game.c for the
 * message format.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buttons.h"
#include "chess.h"

typedef struct {
    struct_pieza board[5][5];
    Jugador jugador;        /* our color */
    int cursor_x;
    int cursor_y;
    bool seleccionada;      /* a piece is picked up... */
    int sel_x;              /* ...from this square */
    int sel_y;
    uint8_t turn_count;     /* moves made so far: white moves when it's even */
    bool sending;           /* our last move isn't acknowledged yet */
    uint32_t sent_ms;       /* when we last sent it */
} game_t;

/* New board from seed; blancas: we play white. */
void game_start(game_t *game, uint32_t seed, bool blancas);

bool game_my_turn(const game_t *game);

/* Joystick moves the cursor. On our turn, KEY1 picks a piece up / moves it
 * (and sends the move), KEY2 puts it back down. Other keys are ignored. */
void game_on_button(game_t *game, button_id_t key);

/* Something the other board sent with link_send(). */
void game_on_data(game_t *game, const uint8_t *data, size_t len);

/* Call often (every loop): resends our move until it's acknowledged. */
void game_tick(game_t *game);

#pragma once

/*
 * game: a match as played on this board: the pieces, our color, and the
 * cursor the joystick moves.
 *
 * Local only for now: moves aren't sent to the other board yet. That's
 * match.c's job (Julian), which can take the board from here.
 */

#include <stdbool.h>
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
} game_t;

/* New board from seed; blancas: we play white. */
void game_start(game_t *game, uint32_t seed, bool blancas);

/* Joystick moves the cursor, KEY1 picks a piece up / moves it,
 * KEY2 puts it back down. Other keys are ignored. */
void game_on_button(game_t *game, button_id_t key);

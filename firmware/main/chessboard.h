#pragma once

/*
 * chessboard: the 5x5 board and the rules of our chess (Franco and Fede's logic).
 * Knows nothing about turns, players or the network: match.c does.
 *
 * The board is board[y][x]: y is the row (0 at the top of the screen,
 * white starts on rows 0-1), x is the column. Both go 0..4.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    Alfil,
    Caballo,
    Peon,
    Torre,
    Rey,
    Dama,
    Vacia,
} TiposDePiezaYCasillaDefault;

typedef struct {
    TiposDePiezaYCasillaDefault Pieza;
    bool EsBlanca;
} struct_pieza;

typedef struct {
    bool EsBlanca;
} Jugador;

typedef struct {
    struct_pieza sq[5][5];
} board_t;

/* Size of a board on the wire: one byte per square. */
#define BOARD_BYTES 25

/* The letter shown for a piece: A C P T R D, ' ' for an empty square. */
char pieza_a_char(TiposDePiezaYCasillaDefault p);

/* Random start: 5 white pieces on rows 0-1, mirrored as black on rows 3-4. */
void iniciarTablero(struct_pieza board[5][5]);

/* If moving (x_init, y_init) -> (x_end, y_end) is legal for jugador, makes the
 * move (capture and promotion included) and returns true. If not, leaves the
 * board as it was and returns false. */
bool movimientoYCaptura(int x_init, int y_init, int x_end, int y_end,
                        struct_pieza board[5][5], Jugador jugador);

/* The board as BOARD_BYTES bytes, row by row (y * 5 + x): the piece in the
 * low bits, plus 0x80 if white. */
void board_to_bytes(const board_t *board, uint8_t out[BOARD_BYTES]);
void board_from_bytes(board_t *board, const uint8_t in[BOARD_BYTES]);

#pragma once

/*
 * chess: the rules of our 5x5 chess (Franco and Fede's board logic).
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

/* The letter shown for a piece: A C P T R D, ' ' for an empty square. */
char pieza_a_char(TiposDePiezaYCasillaDefault p);

/* Random start: 5 white pieces on rows 0-1, mirrored as black on rows 3-4. */
void iniciarTablero(struct_pieza board[5][5]);

/* If moving (x_init, y_init) -> (x_end, y_end) is legal for jugador, makes the
 * move (capture and promotion included) and returns true. If not, leaves the
 * board as it was and returns false. */
bool movimientoYCaptura(int x_init, int y_init, int x_end, int y_end,
                        struct_pieza board[5][5], Jugador jugador);

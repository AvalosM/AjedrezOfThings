#include <stdlib.h>

#include "esp_random.h"

#include "chessboard.h"

static const char s_pieza_char[] = {
    [Alfil]   = 'A',
    [Caballo] = 'C',
    [Peon]    = 'P',
    [Torre]   = 'T',
    [Rey]     = 'R',
    [Dama]    = 'D',
    [Vacia]   = ' ',
};

char pieza_a_char(TiposDePiezaYCasillaDefault p)
{
    if (p < 0 || p > Vacia) {
        return '?';
    }
    return s_pieza_char[p];
}

static bool camino_libre(int x_init, int y_init, int x_end, int y_end, struct_pieza board[5][5])
{
    int dx = (x_end > x_init) - (x_end < x_init);   // -1, 0 o 1
    int dy = (y_end > y_init) - (y_end < y_init);

    int x = x_init + dx;
    int y = y_init + dy;
    while (x != x_end || y != y_end) {
        if (board[y][x].Pieza != Vacia) {
            return false;
        }
        x += dx;
        y += dy;
    }
    return true;
}

void iniciarTablero(struct_pieza board[5][5])
{
    int piezasAiniciar = 5;

    // 1. Vaciar el tablero
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            board[y][x] = (struct_pieza){.Pieza = Vacia, .EsBlanca = false};
        }
    }

    // 2. Lista de las 10 casillas posibles para las blancas (filas 0 y 1)
    int casillas[10];
    for (int i = 0; i < 10; i++) {
        casillas[i] = i;          // casilla i -> fila i / 5, columna i % 5
    }

    // 3. Mezclarlas al azar (Fisher-Yates) para elegir posiciones sin repetir
    for (int i = 9; i > 0; i--) {
        int j = esp_random() % (i + 1);
        int tmp = casillas[i];
        casillas[i] = casillas[j];
        casillas[j] = tmp;
    }

    // 4. Colocar cada pieza blanca y su espejo negro
    for (int i = 0; i < piezasAiniciar; i++) {
        TiposDePiezaYCasillaDefault tipo = esp_random() % Vacia;  // de Alfil a Dama, nunca Vacia

        int y = casillas[i] / 5;
        int x = casillas[i] % 5;

        board[y][x]     = (struct_pieza){.Pieza = tipo, .EsBlanca = true};   // blanca
        board[4 - y][x] = (struct_pieza){.Pieza = tipo, .EsBlanca = false};  // negra, espejada
    }
}

bool movimientoYCaptura(int x_init, int y_init, int x_end, int y_end,
                        struct_pieza board[5][5], Jugador jugador)
{
    if (x_init == x_end && y_init == y_end) {
        return false;
    }

    struct_pieza piezaAMover = board[y_init][x_init];
    struct_pieza piezaAComer = board[y_end][x_end];
    bool destinoVacio = piezaAComer.Pieza == Vacia;

    if (piezaAMover.Pieza == Vacia || piezaAMover.EsBlanca != jugador.EsBlanca) {
        return false;
    }
    if (!destinoVacio && piezaAComer.EsBlanca == jugador.EsBlanca) {
        return false;
    }

    int dx = x_end - x_init;
    int dy = y_end - y_init;
    int adx = abs(dx);
    int ady = abs(dy);
    bool valido = false;

    switch (piezaAMover.Pieza) {
    case Torre:     // en línea recta
        valido = (dx == 0 || dy == 0) && camino_libre(x_init, y_init, x_end, y_end, board);
        break;

    case Alfil:     // en diagonal
        valido = (adx == ady) && camino_libre(x_init, y_init, x_end, y_end, board);
        break;

    case Dama:      // torre + alfil
        valido = (dx == 0 || dy == 0 || adx == ady) && camino_libre(x_init, y_init, x_end, y_end, board);
        break;

    case Caballo:   // en "L", puede saltar piezas
        valido = (adx == 1 && ady == 2) || (adx == 2 && ady == 1);
        break;

    case Rey:       // una casilla en cualquier dirección
        valido = adx <= 1 && ady <= 1;
        break;

    case Peon: {
        // Las blancas avanzan hacia y creciente (empiezan arriba, en la fila 0).
        int adelante = piezaAMover.EsBlanca ? 1 : -1;
        if (dx == 0 && dy == adelante && destinoVacio) {
            valido = true;                       // avanza una casilla
        } else if (adx == 1 && dy == adelante && !destinoVacio) {
            valido = true;                       // come en diagonal
        }
        break;
    }

    default:
        break;
    }

    if (!valido) {
        return false;
    }

    // Ejecuta la jugada
    board[y_end][x_end] = piezaAMover;
    board[y_init][x_init] = (struct_pieza){.Pieza = Vacia, .EsBlanca = false};

    // Coronación: el peón que llega a la última fila se convierte en dama
    if (piezaAMover.Pieza == Peon && (y_end == 0 || y_end == 4)) {
        board[y_end][x_end].Pieza = Dama;
    }

    return true;
}

#define WHITE_BIT 0x80

void board_to_bytes(const board_t *board, uint8_t out[BOARD_BYTES])
{
    for (int i = 0; i < BOARD_BYTES; i++) {
        const struct_pieza p = board->sq[i / 5][i % 5];
        out[i] = p.Pieza | (p.EsBlanca ? WHITE_BIT : 0);
    }
}

void board_from_bytes(board_t *board, const uint8_t in[BOARD_BYTES])
{
    for (int i = 0; i < BOARD_BYTES; i++) {
        const uint8_t piece = in[i] & ~WHITE_BIT;
        board->sq[i / 5][i % 5] = (struct_pieza){
            .Pieza = piece <= Vacia ? piece : Vacia,  /* garbage becomes an empty square */
            .EsBlanca = in[i] & WHITE_BIT,
        };
    }
}

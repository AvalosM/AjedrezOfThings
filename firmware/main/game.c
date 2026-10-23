#include <string.h>

#include "game.h"

void game_start(game_t *game, uint32_t seed, bool blancas)
{
    memset(game, 0, sizeof(*game));
    iniciarTablero(game->board, seed);
    game->jugador.EsBlanca = blancas;
}

void game_on_button(game_t *game, button_id_t key)
{
    /* Joystick: same mapping as Franco and Fede's draw_ui(). */
    if (key == BTN_DOWN && game->cursor_y > 0) {
        game->cursor_y--;
    } else if (key == BTN_RIGHT && game->cursor_x > 0) {
        game->cursor_x--;
    } else if (key == BTN_LEFT && game->cursor_x < 4) {
        game->cursor_x++;
    } else if (key == BTN_UP && game->cursor_y < 4) {
        game->cursor_y++;
    } else if (key == BTN_KEY1) {
        if (!game->seleccionada) {
            game->seleccionada = true;
            game->sel_x = game->cursor_x;
            game->sel_y = game->cursor_y;
        } else {
            /* Makes the move if it's legal; if not, nothing happens (TODO: tell the player). */
            movimientoYCaptura(game->sel_x, game->sel_y, game->cursor_x, game->cursor_y,
                               game->board, game->jugador);
            game->seleccionada = false;
        }
    } else if (key == BTN_KEY2) {
        game->seleccionada = false;
    }
}

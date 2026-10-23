#include <stdio.h>

#include "display.h"
#include "board.h"
#include "game.h"

#include "screens.h"


#define PEER_ROWS 4
#define BUTTON_SLOTS 4
#define BUTTON_SLOT_W BOARD_LCD_WIDTH / BUTTON_SLOTS
#define BUTTON_SLOT_CHARS BUTTON_SLOT_W / DISPLAY_FONT_W

/* Inverted bar across the top. */
static void title(const char *text)
{
    display_text(1, 1, text, true);
}

static void display_button_options(const char **options, int len) {
    const int y = BOARD_LCD_HEIGHT - DISPLAY_FONT_H - 1;
    for (int i = 0; i < len && i < BUTTON_SLOTS; i++) {
        int x = i * BUTTON_SLOT_W;
        char label[BUTTON_SLOT_CHARS + 1];
        
        /* Draw option */
        snprintf(label, sizeof(label), "%s", options[i]);
        display_text(x, y, label, false);
    }
}

void screen_lobby(const char *me, const link_peer_t *nearby, int n, int selected)
{
    char line[24];

    display_clear();
    snprintf(line, sizeof(line), "LOBBY  me:%s", me);
    title(line);

    if (n == 0) {
        display_text(0, 16, "nobody nearby...", false);
    }

    /* First row to draw: 0 until the selection passes the last row, then
       scroll so the selected board stays on the bottom row. */
    int first = selected < PEER_ROWS ? 0 : selected - PEER_ROWS + 1;
    for (int i = first; i < n && i < first + PEER_ROWS; i++) {
        snprintf(line, sizeof(line), "%c %s %4ddBm%s", i == selected ? '>' : ' ',
                 nearby[i].name, nearby[i].rssi, nearby[i].busy ? " busy" : "");
        display_text(0, 14 + (i - first) * 9, line, false);
    }
    if (first + PEER_ROWS < n) {
        display_text(120, 41, "v", false);  /* more below */
    }

    display_button_options((const char *[]){"ok", "next"}, 2);
    display_flush();
}

void screen_inviting(const link_peer_t *peer)
{
    display_clear();
    title("INVITING");
    char line[24];
    snprintf(line, sizeof(line), "waiting for %s", peer->name);
    display_text(0, 16, line, false);
    display_button_options((const char *[]){"cancel"}, 1);
    display_flush();
}

void screen_invited(const link_peer_t *peer)
{
    display_clear();
    title("INVITED");
    char line[24];
    snprintf(line, sizeof(line), "invited by %s", peer->name);
    display_text(0, 16, line, false);
    display_button_options((const char *[]){"yes", "no"}, 2);
    display_flush();
}

void screen_connected(const link_peer_t *peer)
{
    display_clear();
    title("CONNECTED");
    char line[24];
    snprintf(line, sizeof(line), "connected to %s", peer->name);
    display_text(0, 16, line, false);
    display_button_options((const char *[]){"hang"}, 1);
    display_flush();
}

/* One board square: outlined if empty or white, filled if black. */
static void draw_key(int x, int y, int size, bool pressed)
{
    if (pressed) {
        display_fill_rect(x, y, size, size, true);
    } else {
        display_fill_rect(x, y, size, size, false);
        display_draw_rect(x, y, size, size);
    }
}

void screen_match(const link_peer_t *peer, const game_t *game)
{
    char line[24];

    display_clear();

    // Tablero: casillas de 10 px cada 11 px, desde (0, 6). La casilla del
    // cursor se dibuja sin borde.
    int y_pos = 6;
    for (int y = 0; y < 5; y++) {
        int x_pos = 0;
        for (int x = 0; x < 5; x++) {
            const struct_pieza casilla = game->board[y][x];
            const bool es_cursor = x == game->cursor_x && y == game->cursor_y;
            char txt[2] = {pieza_a_char(casilla.Pieza), '\0'};

            if (casilla.Pieza == Vacia || casilla.EsBlanca) {
                if (!es_cursor) {
                    draw_key(x_pos, y_pos, 10, false);
                }
                if (casilla.Pieza != Vacia) {
                    display_text_pieza(x_pos + 2, y_pos + 2, txt, false);
                }
            } else {
                if (!es_cursor) {
                    draw_key(x_pos, y_pos, 10, true);
                }
                display_text_pieza(x_pos + 2, y_pos + 2, txt, true);
            }
            x_pos += 11;
        }
        y_pos += 11;
    }

    // Right of the board: who we play, our color, and the keys.
    snprintf(line, sizeof(line), "vs %s", peer->name);
    display_text(62, 6, line, false);
    display_text(62, 16, game->jugador.EsBlanca ? "you: white" : "you: black", false);
    if (game->seleccionada) {
        display_text(62, 26, "selected", false);
    }
    display_text(62, 36, "K1 move", false);
    display_text(62, 46, "K2 back", false);
    display_text(62, 56, "K4 quit", false);
    display_flush();
}
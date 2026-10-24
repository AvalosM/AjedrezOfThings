#include <stdio.h>
#include <string.h>

#include "display.h"
#include "board.h"
#include "match.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "screens.h"


#define PEER_ROWS 4
#define BUTTON_SLOTS 4
#define BUTTON_SLOT_W (BOARD_LCD_WIDTH / BUTTON_SLOTS)
#define BUTTON_SLOT_CHARS (BUTTON_SLOT_W / DISPLAY_FONT_W)
#define BLINK_INTERVAL_MS 300
#define ALERT_TIME_MS 1500
#define UI_PERIOD_MS 10

/* Inverted bar across the top. */
static void title(const char *text)
{
    display_text(1, 1, text, true);
}

static void display_text_centered(int y, const char *str, bool invert)
{
    int len = strlen(str);
    int x = (BOARD_LCD_WIDTH - len * DISPLAY_FONT_W) / 2;
    if (x < 0) {
        x = 0;
    }
    display_text(x, y, str, invert);
}

static void display_button_options(const char **options, int len) {
    const int y = BOARD_LCD_HEIGHT - DISPLAY_FONT_H - 1;
    for (int i = 0; i < len && i < BUTTON_SLOTS; i++) {
        int x = i * BUTTON_SLOT_W;
        char label[16];
        
        /* Draw option */
        snprintf(label, sizeof(label), "%s", options[i]);
        display_text(x, y, label, false);
    }
}

void screen_welcome(const char *name)
{
    display_clear();

    display_text(34, 8, "BIENVENIDO", false);
    display_draw_rect(44, 22, 40, 18);
    display_text(55, 27, name, false);

    display_button_options((const char *[]){"ok", "edit"}, 2);
    display_flush();
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

int invalid_move_alert_time = 0;

void screen_match(const match_t *match, int cursor_x, int cursor_y, int picked_x, int picked_y, bool picked, bool is_invalid_move)
{
    char line[24];
    const bool blink_on = (xTaskGetTickCount() * portTICK_PERIOD_MS / BLINK_INTERVAL_MS) % 2;

    display_clear();

    if (match->is_game_over) {
        const bool i_won = (match->winner_name[0] != '\0' &&
                            strcmp(match->winner_name, match->my_name) == 0);

        if (i_won) {
            display_text_centered(8, "Ganaste!", true);
            if (match->game_over_reason == GAME_OVER_TIMEOUT) {
                snprintf(line, sizeof(line), "%s se quedo", match->rival.name);
                display_text_centered(24, line, false);
                display_text_centered(36, "sin tiempo", false);
            } else if (match->game_over_reason == GAME_OVER_NO_PIECES) {
                display_text_centered(24, "Comiste todas", false);
                display_text_centered(36, "las piezas", false);
            } else {
                snprintf(line, sizeof(line), "Gana: %s", match->winner_name);
                display_text_centered(28, line, false);
            }
        } else {
            display_text_centered(8, "Perdiste!", true);
            if (match->game_over_reason == GAME_OVER_TIMEOUT) {
                display_text_centered(24, "Te quedaste", false);
                display_text_centered(36, "sin tiempo", false);
            } else if (match->game_over_reason == GAME_OVER_NO_PIECES) {
                display_text_centered(24, "Te quedaste", false);
                display_text_centered(36, "sin piezas", false);
            } else {
                snprintf(line, sizeof(line), "Gana: %s", match->winner_name);
                display_text_centered(28, line, false);
            }
        }

        display_text_centered(52, "K4: volver", false);
        display_flush();
        return;
    }

    if (is_invalid_move) {
        invalid_move_alert_time = ALERT_TIME_MS;
    }

    if (invalid_move_alert_time > 0) {
        display_text(62, 36,"Movimiento", false);
        display_text(62, 46, "Invalido", false);
        invalid_move_alert_time -= UI_PERIOD_MS;
    } else {
        invalid_move_alert_time = 0;
    }

    // Board: squares of 11 px every 12 px, from (0, 2). The cursor
    // square is drawn without a border.
    int y_pos = 2;
    for (int y = 0; y < 5; y++) {
        int x_pos = 0;
        for (int x = 0; x < 5; x++) {
            const piece_t square = match->board.sq[y][x];
            const bool is_cursor = x == cursor_x && y == cursor_y;
            char txt[2] = {piece_to_char(square.piece), '\0'};

            if (square.piece == PIECE_EMPTY || square.is_white) {
                if (!is_cursor) {
                    if (picked && x == picked_x && y == picked_y) {
                        if (blink_on) {
                            draw_key(x_pos, y_pos, 11, false);
                        }
                    } else {
                        draw_key(x_pos, y_pos, 11, false);
                    }
                }
                if (square.piece != PIECE_EMPTY) {
                    display_text_piece(x_pos + 3, y_pos + 2, txt, false);
                }
            } else {
                if (!is_cursor) {
                    if (picked && x == picked_x && y == picked_y) {
                        if (blink_on) {
                            draw_key(x_pos, y_pos, 11, true);
                        }
                    } else {
                        draw_key(x_pos, y_pos, 11, true);
                    }
                }
                display_text_piece(x_pos + 3, y_pos + 2, txt, true);
            }
            x_pos += 12;
        }
        y_pos += 12;
    }

    // Right of the board: who we play, our color, whose turn, timer, and keys.
    snprintf(line, sizeof(line), "vs %s", match->rival.name);
    display_text(62, 6, line, false);
    display_text(62, 16, match->my_color == COLOR_WHITE ? "you: white" : "you: black", false);
    if (!match->started) {
        display_text(62, 26, "waiting...", false);
    } else {
        int m = match->my_time_s / 60;
        int s = match->my_time_s % 60;
        snprintf(line, sizeof(line), "%s %02d:%02d", match_my_turn() ? "YOU" : "THEM", m, s);
        display_text(62, 26, line, match_my_turn());
    }
    display_text(62, 56, "K4 quit", false);
    display_flush();
}

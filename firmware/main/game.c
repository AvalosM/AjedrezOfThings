#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "link.h"

#include "game.h"

static const char *TAG = "game";

#define RESEND_MS 300

/*
 * Messages to the other board, sent with link_send():
 *
 *   MOVE  type=1, ply, board[25]   the board after move number `ply`
 *   ACK   type=2, ply              "I have move number `ply`"
 *
 * board[] goes row by row (board[y * 5 + x]). Each square is one byte: the
 * piece (TiposDePiezaYCasillaDefault) in the low bits, plus 0x80 if white.
 */
enum { MSG_MOVE = 1, MSG_ACK = 2 };

#define WHITE_BIT 0x80

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t ply;
    uint8_t board[25];
} msg_move_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t ply;
} msg_ack_t;

static uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void send_move(game_t *game)
{
    msg_move_t msg = {.type = MSG_MOVE, .ply = game->ply};
    for (int i = 0; i < 25; i++) {
        const struct_pieza p = game->board[i / 5][i % 5];
        msg.board[i] = p.Pieza | (p.EsBlanca ? WHITE_BIT : 0);
    }
    link_send(&msg, sizeof(msg));
    game->sent_ms = now_ms();
}

static void send_ack(uint8_t ply)
{
    const msg_ack_t msg = {.type = MSG_ACK, .ply = ply};
    link_send(&msg, sizeof(msg));
}

void game_start(game_t *game, uint32_t seed, bool blancas)
{
    memset(game, 0, sizeof(*game));
    iniciarTablero(game->board, seed);
    game->jugador.EsBlanca = blancas;
}

bool game_my_turn(const game_t *game)
{
    const bool white_to_move = game->ply % 2 == 0;
    return white_to_move == game->jugador.EsBlanca;
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
    } else if (key == BTN_KEY1 && game_my_turn(game)) {
        if (!game->seleccionada) {
            game->seleccionada = true;
            game->sel_x = game->cursor_x;
            game->sel_y = game->cursor_y;
        } else {
            /* Illegal moves do nothing (TODO: tell the player). */
            if (movimientoYCaptura(game->sel_x, game->sel_y, game->cursor_x, game->cursor_y,
                                   game->board, game->jugador)) {
                game->ply++;
                game->sending = true;
                send_move(game);
            }
            game->seleccionada = false;
        }
    } else if (key == BTN_KEY2) {
        game->seleccionada = false;
    }
}

void game_on_data(game_t *game, const uint8_t *data, size_t len)
{
    if (len == sizeof(msg_move_t) && data[0] == MSG_MOVE) {
        const msg_move_t *msg = (const msg_move_t *)data;

        if (msg->ply == game->ply + 1 && !game_my_turn(game)) {
            /* Their move: take their board. */
            for (int i = 0; i < 25; i++) {
                const uint8_t piece = msg->board[i] & ~WHITE_BIT;
                game->board[i / 5][i % 5] = (struct_pieza){
                    .Pieza = piece <= Vacia ? piece : Vacia,
                    .EsBlanca = msg->board[i] & WHITE_BIT,
                };
            }
            game->ply = msg->ply;
            game->sending = false;  /* they could only move after getting ours */
            ESP_LOGI(TAG, "got move %d", game->ply);
        }
        if (msg->ply <= game->ply) {
            send_ack(msg->ply);  /* new or a repeat: they resend until we answer */
        }
    } else if (len == sizeof(msg_ack_t) && data[0] == MSG_ACK) {
        const msg_ack_t *msg = (const msg_ack_t *)data;
        if (game->sending && msg->ply == game->ply) {
            game->sending = false;
            ESP_LOGI(TAG, "move %d acknowledged", game->ply);
        }
    }
}

void game_tick(game_t *game)
{
    if (game->sending && now_ms() - game->sent_ms >= RESEND_MS) {
        send_move(game);
    }
}

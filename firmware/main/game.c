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
 *   START type=3, turn_count=0, board[25]   the starting board (white sends it)
 *   MOVE  type=1, turn_count, board[25]     the board after move number turn_count
 *   ACK   type=2, turn_count                "I have the board for turn_count"
 *
 * START and MOVE are resent every 300 ms until the ACK arrives, and only one
 * is in flight at a time. board[] goes row by row (board[y * 5 + x]). Each
 * square is one byte: the piece (TiposDePiezaYCasillaDefault) in the low
 * bits, plus 0x80 if white.
 */
enum { MSG_MOVE = 1, MSG_ACK = 2, MSG_START = 3 };

#define WHITE_BIT 0x80

typedef struct __attribute__((packed)) {
    uint8_t type;           /* MSG_START or MSG_MOVE */
    uint8_t turn_count;
    uint8_t board[25];
} msg_board_t;

typedef struct __attribute__((packed)) {
    uint8_t type;           /* MSG_ACK */
    uint8_t turn_count;
} msg_ack_t;

static uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* START before the first move, MOVE after. */
static void send_board(game_t *game)
{
    msg_board_t msg = {
        .type = game->turn_count == 0 ? MSG_START : MSG_MOVE,
        .turn_count = game->turn_count,
    };
    for (int i = 0; i < 25; i++) {
        const struct_pieza p = game->board[i / 5][i % 5];
        msg.board[i] = p.Pieza | (p.EsBlanca ? WHITE_BIT : 0);
    }
    link_send(&msg, sizeof(msg));
    game->sent_ms = now_ms();
}

static void take_board(game_t *game, const msg_board_t *msg)
{
    for (int i = 0; i < 25; i++) {
        const uint8_t piece = msg->board[i] & ~WHITE_BIT;
        game->board[i / 5][i % 5] = (struct_pieza){
            .Pieza = piece <= Vacia ? piece : Vacia,
            .EsBlanca = msg->board[i] & WHITE_BIT,
        };
    }
    game->turn_count = msg->turn_count;
}

static void send_ack(uint8_t turn_count)
{
    const msg_ack_t msg = {.type = MSG_ACK, .turn_count = turn_count};
    link_send(&msg, sizeof(msg));
}

void game_start(game_t *game, bool blancas)
{
    memset(game, 0, sizeof(*game));
    game->jugador.EsBlanca = blancas;

    /* White makes the board and sends it; black waits for it. */
    if (blancas) {
        iniciarTablero(game->board);
        game->started = true;
        game->sending = true;
        send_board(game);
    }
}

bool game_my_turn(const game_t *game)
{
    const bool white_to_move = game->turn_count % 2 == 0;
    return game->started && white_to_move == game->jugador.EsBlanca;
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
    } else if (key == BTN_KEY1 && game_my_turn(game) && !game->sending) {
        if (!game->seleccionada) {
            game->seleccionada = true;
            game->sel_x = game->cursor_x;
            game->sel_y = game->cursor_y;
        } else {
            /* Illegal moves do nothing (TODO: tell the player). */
            if (movimientoYCaptura(game->sel_x, game->sel_y, game->cursor_x, game->cursor_y,
                                   game->board, game->jugador)) {
                game->turn_count++;
                game->sending = true;
                send_board(game);
            }
            game->seleccionada = false;
        }
    } else if (key == BTN_KEY2) {
        game->seleccionada = false;
    }
}

void game_on_data(game_t *game, const uint8_t *data, size_t len)
{
    if (len == sizeof(msg_board_t) && (data[0] == MSG_START || data[0] == MSG_MOVE)) {
        const msg_board_t *msg = (const msg_board_t *)data;

        bool take;
        if (data[0] == MSG_START) {
            take = !game->started;  /* the starting board, only once */
        } else {
            take = game->started && msg->turn_count == game->turn_count + 1 && !game_my_turn(game);
        }
        if (take) {
            take_board(game, msg);
            game->started = true;
            game->sending = false;  /* they could only send this after getting ours */
            ESP_LOGI(TAG, "got the board for turn %d", game->turn_count);
        }
        if (game->started && msg->turn_count <= game->turn_count) {
            send_ack(msg->turn_count);  /* new or a repeat: they resend until we answer */
        }
    } else if (len == sizeof(msg_ack_t) && data[0] == MSG_ACK) {
        const msg_ack_t *msg = (const msg_ack_t *)data;
        if (game->sending && msg->turn_count == game->turn_count) {
            game->sending = false;
            ESP_LOGI(TAG, "turn %d acknowledged", game->turn_count);
        }
    }
}

void game_tick(game_t *game)
{
    if (game->sending && now_ms() - game->sent_ms >= RESEND_MS) {
        send_board(game);
    }
}

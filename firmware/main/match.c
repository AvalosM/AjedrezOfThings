#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "match.h"

static const char *TAG = "match";

#define STATE_PERIOD_MS 300

enum { MSG_STATE = 1 };

typedef struct __attribute__((packed)) {
    uint8_t type;               /* MSG_STATE */
    uint8_t turn_count;
    uint8_t board[BOARD_BYTES];
} msg_state_t;

static match_t s_match;
static uint32_t s_sent_ms;      /* when we last sent our STATE */

static uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void send_state(void)
{
    msg_state_t msg = {.type = MSG_STATE, .turn_count = s_match.turn_count};
    board_to_bytes(&s_match.board, msg.board);
    link_send(&msg, sizeof(msg));
    s_sent_ms = now_ms();
}

static void start(player_color_t color, const link_peer_t *rival)
{
    memset(&s_match, 0, sizeof(s_match));
    s_match.active = true;
    s_match.my_color = color;
    s_match.rival = *rival;

    /* White makes the board; black gets it from white's first STATE. */
    if (color == COLOR_WHITE) {
        iniciarTablero(s_match.board.sq);
        s_match.started = true;
        send_state();
    }
    ESP_LOGI(TAG, "playing %s as %s", rival->name, color == COLOR_WHITE ? "white" : "black");
}

/* The rival's state: take its board if it's newer than ours. */
static void on_state(const msg_state_t *msg)
{
    if (s_match.started && msg->turn_count <= s_match.turn_count) {
        return;  /* nothing new: we already have this turn */
    }
    board_from_bytes(&s_match.board, msg->board);
    s_match.turn_count = msg->turn_count;
    s_match.started = true;
    ESP_LOGI(TAG, "board for turn %d", s_match.turn_count);
}

esp_err_t match_accept(void)
{
    link_peer_t rival;
    if (link_get_state(&rival) != LINK_INVITED || link_accept() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    start(COLOR_BLACK, &rival);
    return ESP_OK;
}

void match_quit(void)
{
    s_match.active = false;
    link_hangup();
}

void match_on_link_event(link_event_t ev, const link_peer_t *peer, const uint8_t *data, size_t len)
{
    switch (ev) {
    case LINK_EVT_CONNECTED:
        start(COLOR_WHITE, peer);  /* our invite was accepted: we play white */
        break;
    case LINK_EVT_DISCONNECTED:
        s_match.active = false;
        break;
    case LINK_EVT_DATA:
        if (s_match.active && len == sizeof(msg_state_t) && data[0] == MSG_STATE) {
            on_state((const msg_state_t *)data);
        }
        break;
    case LINK_EVT_INVITED:
        break;  /* the player decides: match_accept() */
    }
}

void match_tick(void)
{
    if (s_match.active && s_match.started && now_ms() - s_sent_ms >= STATE_PERIOD_MS) {
        send_state();
    }
}

bool match_my_turn(void)
{
    const bool white_to_move = s_match.turn_count % 2 == 0;
    return s_match.active && s_match.started && white_to_move == (s_match.my_color == COLOR_WHITE);
}

bool match_move(int x0, int y0, int x1, int y1)
{
    const Jugador me = {.EsBlanca = s_match.my_color == COLOR_WHITE};

    if (!match_my_turn() || !movimientoYCaptura(x0, y0, x1, y1, s_match.board.sq, me)) {
        return false;
    }
    s_match.turn_count++;
    send_state();  /* right away, not at the next tick */
    return true;
}

const match_t *match_get(void)
{
    return &s_match;
}

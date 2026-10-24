/*
 * AjedrezOfThings firmware: start-up and the main loop.
 *
 * The network side is done: components/link finds nearby boards and handles
 * invite / accept / hang up (see link.h). What's left here is the business
 * logic, marked TODO:
 *   - on_button():     what each key does on each screen
 *   - on_link_event(): reacting to invites, connections and messages
 *   - screens.c:       what each screen shows
 *
 * Joystick note: buttons.c names its directions as in the organizers' board.h,
 * which are shifted 90 degrees. Pushing up arrives as BTN_RIGHT, right as
 * BTN_DOWN, down as BTN_LEFT, left as BTN_UP. The keys (BTN_KEY1..4) are fine.
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "buttons.h"
#include "display.h"
#include "events.h"
#include "link.h"
#include "match.h"
#include "player.h"
#include "screens.h"

static const char *TAG = "main";

#define UI_PERIOD_MS 100    /* redraw at least this often */
#define MAX_LISTED   16     /* boards in the lobby list (the screen scrolls) */

static char s_name[LINK_NAME_LEN + 1];
char player_tag[PLAYER_NAME_LEN];
static int s_selected;      /* lobby: index of the highlighted board in link_nearby() */

/* Match screen: the square the joystick points at, and the piece picked up. */
static int s_cursor_x;
static int s_cursor_y;
static bool s_picked;
static int s_pick_x;
static int s_pick_y;
static int is_invalid_move;

/* Arcade-style initials from the MAC, so every board gets its own name. */
static void name_from_mac(char out[LINK_NAME_LEN + 1])
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    for (int i = 0; i < LINK_NAME_LEN; i++) {
        out[i] = 'A' + mac[3 + i] % 26;
    }
    out[LINK_NAME_LEN] = '\0';
}

/* Joystick moves the cursor. KEY1 picks up a piece (on our turn), then KEY1
 * again asks match to move it; KEY2 puts it back down. */
static void on_match_button(button_id_t key)
{
    /* Joystick: same mapping as Franco and Fede's draw_ui(). */
    if (key == BTN_DOWN && s_cursor_y > 0) {
        s_cursor_y--;
    } else if (key == BTN_RIGHT && s_cursor_x > 0) {
        s_cursor_x--;
    } else if (key == BTN_LEFT && s_cursor_x < 4) {
        s_cursor_x++;
    } else if (key == BTN_UP && s_cursor_y < 4) {
        s_cursor_y++;
    } else if (key == BTN_KEY1 && !s_picked) {
        if (match_my_turn()) {
            bool imwhite = match_get()->my_color == COLOR_WHITE;
            bool square_is_white = match_get()->board.sq[s_cursor_y][s_cursor_x].is_white;
            bool square_is_empty = match_get()->board.sq[s_cursor_y][s_cursor_x].piece == PIECE_EMPTY;
            if (square_is_white == imwhite && !square_is_empty) {
                s_picked = true;
                s_pick_x = s_cursor_x;
                s_pick_y = s_cursor_y;
            }
        }
    } else if (key == BTN_KEY1) {
        /* Illegal moves do nothing (TODO: tell the player). */
        if (!match_move(s_pick_x, s_pick_y, s_cursor_x, s_cursor_y)) {
            is_invalid_move = true;
        };
        s_picked = false;
    } else if (key == BTN_KEY2) {
        s_picked = false;
    }
}

static void on_button(button_id_t key)
{
    switch (link_get_state(NULL)) {
    case LINK_IDLE: {
        link_peer_t nearby[MAX_LISTED];
        const int n = link_nearby(nearby, MAX_LISTED);
        if (n == 0) {
            break;
        }
        if (s_selected >= n) {
            s_selected = 0;  /* a board left the list */
        }
        if (key == BTN_KEY2) {
            s_selected = (s_selected + 1) % n;
        } else if (key == BTN_KEY1 && !nearby[s_selected].busy) {
            link_invite(&nearby[s_selected]);  /* busy boards wouldn't answer */
        }
        break;
    }
    case LINK_INVITING:
        if (key == BTN_KEY1 || key == BTN_KEY2) {
            link_hangup();
        }
        break;
    case LINK_INVITED:
        if (key == BTN_KEY1) {
            s_picked = false;
            match_accept();  /* we play black */
        } else if (key == BTN_KEY2) {
            link_hangup();
        }
        break;
    case LINK_CONNECTED:
        if (match_get()->is_game_over) {
            if (key == BTN_KEY4 || key == BTN_KEY1) {
                match_quit();
            }
        } else if (key == BTN_KEY4) {
            match_quit();
        } else {
            on_match_button(key);
        }
        break;
    }
}

static void on_link_event(const event_t *ev)
{
    match_on_link_event(ev->link, &ev->peer, ev->data, ev->len);

    switch (ev->link) {
    case LINK_EVT_INVITED:
        ESP_LOGI(TAG, "%s invites us", ev->peer.name);
        /* TODO: a sound? */
        break;
    case LINK_EVT_CONNECTED:
        ESP_LOGI(TAG, "%s accepted", ev->peer.name);
        s_picked = false;
        break;
    case LINK_EVT_DISCONNECTED:
        ESP_LOGI(TAG, "%s is gone", ev->peer.name);
        break;
    case LINK_EVT_DATA:
        break;
    }
}

/* Draw the screen for whatever state the link is in. */
static void draw(void)
{
    link_peer_t peer;
    link_peer_t nearby[MAX_LISTED];

    switch (link_get_state(&peer)) {
    case LINK_IDLE: {
        const int n = link_nearby(nearby, MAX_LISTED);
        if (s_selected >= n) {
            s_selected = 0;  /* a board left the list */
        }
        screen_lobby(s_name, nearby, n, s_selected);
        break;
    }
    case LINK_INVITING:
        screen_inviting(&peer);
        break;
    case LINK_INVITED:
        screen_invited(&peer);
        break;
    case LINK_CONNECTED:
        screen_match(match_get(), s_cursor_x, s_cursor_y, s_pick_x, s_pick_y, s_picked, is_invalid_move);
        if (is_invalid_move) {
            is_invalid_move = false;  // Reset the flag after displaying the alert
        }
        break;
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(buttons_init());

    bool has_name = player_profile_load(player_tag, sizeof(player_tag));
    if (!has_name) {
        name_from_mac(player_tag);
        player_profile_arcade_input(player_tag);
    } else {
        if (player_welcome_screen(player_tag, 3000)) {
            player_profile_arcade_input(player_tag);
        }
    }
    snprintf(s_name, sizeof(s_name), "%s", player_tag);

    events_init();
    match_init(s_name);
    ESP_ERROR_CHECK(link_init(s_name, events_on_link));

    while (1) {
        event_t ev;
        if (events_wait(&ev, UI_PERIOD_MS)) {
            if (ev.kind == EVENT_BUTTON && ev.button.pressed) {
                on_button(ev.button.id);
            } else if (ev.kind == EVENT_LINK) {
                on_link_event(&ev);
            }
        }
        match_tick();
        draw();
    }
}

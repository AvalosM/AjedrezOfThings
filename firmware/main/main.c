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
#include "game.h"
#include "link.h"
#include "player.h"
#include "screens.h"

static const char *TAG = "main";

#define UI_PERIOD_MS 100    /* redraw at least this often */
#define MAX_LISTED   16     /* boards in the lobby list (the screen scrolls) */

static char s_name[LINK_NAME_LEN + 1];
char player_tag[PLAYER_NAME_LEN];
static int s_selected;      /* lobby: index of the highlighted board in link_nearby() */
static game_t s_game;       /* the match being played while connected */

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

/* The seed for the starting board. It only depends on the two MACs, added
 * byte by byte, so both boards of a match get the same number. */
static uint32_t match_seed(const uint8_t peer_mac[6])
{
    uint8_t my_mac[6];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);

    uint32_t seed = 0;
    for (int i = 0; i < 6; i++) {
        seed = seed * 31 + my_mac[i] + peer_mac[i];
    }
    return seed;
}

static void on_button(button_id_t key)
{
    link_peer_t peer;

    switch (link_get_state(&peer)) {
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
        if (key == BTN_KEY2) {
            link_hangup();
        }
        break;
    case LINK_INVITED:
        if (key == BTN_KEY1) {
            if (link_accept() == ESP_OK) {
                game_start(&s_game, match_seed(peer.mac), false);  /* the inviter plays white */
            }
        } else if (key == BTN_KEY2) {
            link_hangup();
        }
        break;
    case LINK_CONNECTED:
        if (key == BTN_KEY4) {
            link_hangup();
        } else {
            game_on_button(&s_game, key);
        }
        break;
    }
}

static void on_link_event(const event_t *ev)
{
    switch (ev->link) {
    case LINK_EVT_INVITED:
        ESP_LOGI(TAG, "%s invites us", ev->peer.name);
        /* TODO: a sound? */
        break;
    case LINK_EVT_CONNECTED:
        ESP_LOGI(TAG, "%s accepted", ev->peer.name);
        game_start(&s_game, match_seed(ev->peer.mac), true);  /* we invited: we play white */
        break;
    case LINK_EVT_DISCONNECTED:
        ESP_LOGI(TAG, "%s is gone", ev->peer.name);
        /* TODO: back to the lobby happens on its own; anything to clean up? */
        break;
    case LINK_EVT_DATA:
        ESP_LOGI(TAG, "%s sent %d bytes", ev->peer.name, (int)ev->len);
        /* TODO: ev->data / ev->len is what the peer sent with link_send(). */
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
        screen_match(&peer, &s_game);
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
        ESP_LOGI(TAG, "Bienvenido de nuevo: %s", player_tag);
    }
    snprintf(s_name, sizeof(s_name), "%s", player_tag);

    events_init();
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
        draw();
    }
}

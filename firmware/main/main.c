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

#include "esp_log.h"
#include "esp_mac.h"

#include "buttons.h"
#include "display.h"
#include "events.h"
#include "link.h"
#include "screens.h"

static const char *TAG = "main";

#define UI_PERIOD_MS 100    /* redraw at least this often */
#define MAX_LISTED   4      /* boards that fit on the lobby screen */

static char s_name[LINK_NAME_LEN + 1];
static int s_selected;      /* lobby: index of the highlighted board in link_nearby() */

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

static void on_button(button_id_t key)
{
    switch (link_get_state(NULL)) {
    case LINK_IDLE:
        /* TODO lobby: move s_selected through link_nearby(), and invite the
         * selected board with link_invite(). Boards with .busy set won't answer. */
        break;
    case LINK_INVITING:
        /* TODO: let the player cancel with link_hangup(). */
        break;
    case LINK_INVITED:
        /* TODO: link_accept(), or link_hangup() to decline. */
        break;
    case LINK_CONNECTED:
        /* TODO: send something with link_send(), or link_hangup(). */
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
        /* TODO: start a game. */
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
    case LINK_IDLE:
        screen_lobby(s_name, nearby, link_nearby(nearby, MAX_LISTED), s_selected);
        break;
    case LINK_INVITING:
        screen_inviting(&peer);
        break;
    case LINK_INVITED:
        screen_invited(&peer);
        break;
    case LINK_CONNECTED:
        screen_connected(&peer);
        break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(buttons_init());
    events_init();

    name_from_mac(s_name);
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

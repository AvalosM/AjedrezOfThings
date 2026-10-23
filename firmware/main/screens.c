#include <stdio.h>

#include "display.h"
#include "board.h"
#include "match.h"

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

void screen_match(const match_t *match, const link_peer_t *peer) {
    display_clear();
    title("MATCH");
    char line[24];
    snprintf(line, sizeof(line), "playing %s", peer->name);
    display_text(0, 16, line, false);
    snprintf(line, sizeof(line), "turn: %d", match->turn_count);
    display_text(0, 26, line, false);
    display_button_options((const char *[]){"hang"}, 1);
    display_flush();
}
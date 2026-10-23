#include <stdio.h>

#include "display.h"

#include "screens.h"

#define PEER_ROWS 4

/* Inverted bar across the top. */
static void title(const char *text)
{
    display_text(1, 1, text, true);
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

    display_text(0, 56, "K1 invite K2 next", false);
    display_flush();
}

void screen_inviting(const link_peer_t *peer)
{
    display_clear();
    title("INVITING");
    char line[24];
    snprintf(line, sizeof(line), "waiting for %s", peer->name);
    display_text(0, 16, line, false);
    display_text(0, 56, "K2 cancel", false);
    display_flush();
}

void screen_invited(const link_peer_t *peer)
{
    display_clear();
    title("INVITED");
    char line[24];
    snprintf(line, sizeof(line), "invited by %s", peer->name);
    display_text(0, 16, line, false);
    display_text(0, 56, "K1 accept  K2 decline", false);
    display_flush();
}

void screen_connected(const link_peer_t *peer)
{
    display_clear();
    title("CONNECTED");
    char line[24];
    snprintf(line, sizeof(line), "connected to %s", peer->name);
    display_text(0, 16, line, false);
    display_text(0, 56, "K2 hangup", false);
    display_flush();
}

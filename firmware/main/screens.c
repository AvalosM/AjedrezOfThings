#include <stdio.h>

#include "display.h"

#include "screens.h"

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
    /* TODO: list the nearby boards (name, signal, busy) and mark the
     * selected one; show which keys do what. */
    snprintf(line, sizeof(line), "%d nearby", n);
    display_text(0, 16, line, false);
    display_flush();
}

void screen_inviting(const link_peer_t *peer)
{
    display_clear();
    title("INVITING");
    /* TODO: "waiting for <peer->name>...", how to cancel. */
    display_text(0, 16, peer->name, false);
    display_flush();
}

void screen_invited(const link_peer_t *peer)
{
    display_clear();
    title("INVITED");
    /* TODO: "<peer->name> wants to play!", how to accept or decline. */
    display_text(0, 16, peer->name, false);
    display_flush();
}

void screen_connected(const link_peer_t *peer)
{
    display_clear();
    title("CONNECTED");
    /* TODO: who we're connected to, and later the game itself. */
    display_text(0, 16, peer->name, false);
    display_flush();
}

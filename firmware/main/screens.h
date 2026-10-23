#pragma once

/*
 * screens: what the display shows, one function per link state.
 * Each one clears the screen, draws, and flushes. 128x64 pixels,
 * 6x8 font: 21 characters by 8 lines.
 */

#include "game.h"
#include "link.h"

void screen_lobby(const char *me, const link_peer_t *nearby, int n, int selected);
void screen_inviting(const link_peer_t *peer);
void screen_invited(const link_peer_t *peer);
void screen_connected(const link_peer_t *peer);
void screen_match(const link_peer_t *peer, const game_t *game);

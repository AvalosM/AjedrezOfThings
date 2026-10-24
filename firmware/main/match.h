#pragma once

/*
 * match: one game between this board and a rival, from start to end. The
 * orchestrator: it starts the game when the link connects, decides whose
 * turn it is, asks chessboard.c whether a move is legal, and keeps both boards
 * in sync over the link. The UI only asks it to move and draws match_get().
 *
 * Sync: while a match runs, each board sends its state every 300 ms:
 *
 *   STATE  type=1, turn_count, board[25]
 *
 * and takes the rival's board whenever its turn_count is newer than ours.
 * There are no ACKs: a lost STATE is replaced by the next one. White (the
 * board that invited) makes the starting board; black waits for it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "chessboard.h"
#include "link.h"

typedef enum {
    COLOR_WHITE = 0,
    COLOR_BLACK = 1
} player_color_t;

typedef enum {
    GAME_OVER_NONE = 0,
    GAME_OVER_TIMEOUT = 1,
    GAME_OVER_NO_PIECES = 2,
} game_over_reason_t;

#define MATCH_TIME_SECONDS 60  

typedef struct {
    bool active;                /* a match is running */
    bool started;               /* we have the board (black waits for white's first STATE) */
    player_color_t my_color;
    char my_name[LINK_NAME_LEN + 1];
    link_peer_t rival;
    uint8_t turn_count;         /* moves made so far: white moves when it's even */
    board_t board;
    int32_t my_time_s;         
    bool is_game_over;     
    game_over_reason_t game_over_reason;
    char winner_name[LINK_NAME_LEN + 1];
    uint32_t game_over_since_ms;
} match_t;

/* Initialize match system with our player name. */
void match_init(const char *my_name);

/* Accept the invite we got: connects the link and starts a match as black. */
esp_err_t match_accept(void);

/* Leave the match and hang up. */
void match_quit(void);

/* Pass every link event here. When our invite is accepted it starts a match
 * as white; it takes the rival's STATE; the match ends when the link drops. */
void match_on_link_event(link_event_t ev, const link_peer_t *peer, const uint8_t *data, size_t len);

/* Call often (every loop): sends our STATE every 300 ms. */
void match_tick(void);

bool match_my_turn(void);

/* Our move from (x0, y0) to (x1, y1). false if it's not our turn or the
 * move is illegal; the board doesn't change then. */
bool match_move(int x0, int y0, int x1, int y1);

/* The current match, read only: for the screen. */
const match_t *match_get(void);

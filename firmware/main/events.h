#pragma once

/*
 * events: one queue for everything the main loop reacts to.
 *
 * Button presses and link events arrive here from their own tasks, so the
 * main loop handles them one at a time, in order, without locks.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buttons.h"
#include "link.h"

typedef enum {
    EVENT_BUTTON,   /* a key or joystick direction was pressed or released */
    EVENT_LINK,     /* something happened on the link (see link_event_t) */
} event_kind_t;

typedef struct {
    event_kind_t kind;

    button_event_t button;          /* EVENT_BUTTON */

    link_event_t link;              /* EVENT_LINK */
    link_peer_t peer;               /* EVENT_LINK: the board it's about */
    uint8_t data[LINK_MAX_DATA];    /* EVENT_LINK + LINK_EVT_DATA: what it sent */
    size_t len;
} event_t;

/* Create the queue and start forwarding button presses into it.
 * Call after buttons_init(). */
void events_init(void);

/* Pass this to link_init(): it puts link events on the queue. */
void events_on_link(link_event_t ev, const link_peer_t *peer, const uint8_t *data, size_t len);

/* Wait up to timeout_ms for the next event. false if none arrived. */
bool events_wait(event_t *out, uint32_t timeout_ms);

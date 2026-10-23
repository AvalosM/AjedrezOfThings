#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "events.h"

#define QUEUE_LEN 16

static QueueHandle_t s_queue;

/* buttons.c has its own queue: copy each of its events into ours. */
static void forward_buttons(void *arg)
{
    QueueHandle_t buttons = buttons_event_queue();
    event_t ev = {.kind = EVENT_BUTTON};

    while (1) {
        xQueueReceive(buttons, &ev.button, portMAX_DELAY);
        xQueueSend(s_queue, &ev, portMAX_DELAY);
    }
}

void events_init(void)
{
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(event_t));
    xTaskCreate(forward_buttons, "events", 3072, NULL, 5, NULL);
}

/* Runs in link's task. */
void events_on_link(link_event_t link_ev, const link_peer_t *peer, const uint8_t *data, size_t len)
{
    event_t ev = {.kind = EVENT_LINK, .link = link_ev, .peer = *peer, .len = len};
    if (len) {
        memcpy(ev.data, data, len);
    }
    xQueueSend(s_queue, &ev, 0);  /* queue full: drop it rather than stall the link */
}

bool events_wait(event_t *out, uint32_t timeout_ms)
{
    return xQueueReceive(s_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

#pragma once

/*
 * radio: Wi-Fi + ESP-NOW setup, the frame format, sending and receiving.
 * Knows nothing about invites or connections; link.c does.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#include "link.h"

/*
 * Every frame: 'A' 'O' 'T' | version | type | flags | name[3] | mac[6] | data...
 * Frames without our magic and version are dropped on arrival: other teams
 * use ESP-NOW on the same channel.
 */
#define FRAME_MAGIC   "AOT"
#define FRAME_VERSION 1
#define FLAG_BUSY     0x01

typedef enum {
    FRAME_HELLO = 1,  /* broadcast every second: I'm here */
    FRAME_INVITE,     /* to one board: want to play? (resent until answered) */
    FRAME_ACCEPT,     /* to the inviter: yes */
    FRAME_PING,       /* to the peer: still here (while connected) */
    FRAME_DATA,       /* to the peer: link_send() payload */
    FRAME_BYE,        /* to the peer: no thanks / cancel / hang up */
} frame_type_t;

typedef struct __attribute__((packed)) {
    uint8_t magic[3];
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    char name[LINK_NAME_LEN];   /* sender's name */
    uint8_t mac[6];             /* sender's MAC */
    uint8_t data[];
} frame_t;

#define FRAME_MAX_LEN (sizeof(frame_t) + LINK_MAX_DATA)

/* A received frame, as radio puts it on the queue. */
typedef struct {
    uint8_t mac[6];     /* who sent it */
    int8_t rssi;
    uint8_t len;        /* bytes in buf */
    uint8_t buf[FRAME_MAX_LEN];
} frame_rx_t;

extern const uint8_t RADIO_BROADCAST[6];

/* Start Wi-Fi and ESP-NOW. Every valid frame received is copied into rxq
 * as a frame_rx_t. name: exactly LINK_NAME_LEN chars, sent in every frame. */
esp_err_t radio_init(const char *name, QueueHandle_t rxq);

/* ESP-NOW only unicasts to registered boards. We register one at a time:
 * this replaces the previous one. */
void radio_set_peer(const uint8_t mac[6]);

/* Send one frame to dest (a MAC, or RADIO_BROADCAST). */
esp_err_t radio_send(const uint8_t dest[6], frame_type_t type, bool busy, const void *data, size_t len);

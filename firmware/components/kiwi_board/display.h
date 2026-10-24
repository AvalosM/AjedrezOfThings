#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define DISPLAY_FONT_W  6
#define DISPLAY_FONT_H  8

#define DISPLAY_FONT_W_PIECE  4
#define DISPLAY_FONT_H_PIECE  6

esp_err_t display_init(void);

void display_clear(void);

void display_set_pixel(int x, int y, bool on);
void display_fill_rect(int x, int y, int w, int h, bool on);
void display_draw_rect(int x, int y, int w, int h);

void display_text(int x, int y, const char *str, bool invert);
void display_text_piece(int x, int y, const char *str, bool invert);
void display_draw_bitmap_8x8(int x, int y, const uint8_t bitmap[8], bool invert);

esp_err_t display_flush(void);

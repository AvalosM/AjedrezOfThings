#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio.h"
#include "buttons.h"
#include "display.h"

static const char *TAG = "demo";

#define UI_PERIOD_MS 100

static const int s_button_freq[BTN_COUNT] = {
    [BTN_UP] = 440,
    [BTN_DOWN] = 659,
    [BTN_LEFT] = 494,
    [BTN_RIGHT] = 587,
    [BTN_CENTER] = 784,
    [BTN_KEY1] = 262,
    [BTN_KEY2] = 330,
    [BTN_KEY3] = 392,
    [BTN_KEY4] = 523,
};

int stick_pos_x = 0;
int stick_pos_y = 0;

static void draw_key(int x, int y, int size, bool pressed)
{
    if (pressed) {
        display_fill_rect(x, y, size, size, true);
    } else {
        display_fill_rect(x, y, size, size, false);
        display_draw_rect(x, y, size, size);
    }
}

static void draw_ui(uint32_t pressed, int mic_level, bool audio_ok)
{
    char line[24];

    display_clear();
    display_text(70, 10, "Ajedrez", true);
    display_text(70, 18, "of", true);
    display_text(70, 26, "things", true);


    bool board[5][5] = {
        {false, false, false, false, false },
        {false, false, false, false, false },
        {false, false, false, false, false },
        {false, false, false, false, false },
        {false, false, false, false, false }
        };

    int x_init = 0;
    int y_init = 6;
    int x_pos = x_init;

    board[stick_pos_y][stick_pos_x] = true;
    
    for (int y = 0; y < 5; y++) {
        x_pos = x_init;
        for (int x = 0; x < 5; x++) {
            draw_key(x_pos, y_init,10, board[y][x]);
            x_pos += 11;
        }
        y_init += 11;
    }

    if (pressed & (1u << BTN_UP)){
        if(stick_pos_y>0){
            stick_pos_y-=1;
        }
    }
    else if (pressed & (1u << BTN_LEFT)){
        if(stick_pos_x>0){
            stick_pos_x-=1;
        }
    }
    else if (pressed & (1u << BTN_RIGHT)){
        if(stick_pos_x<4){
            stick_pos_x+=1;
        }
    }
    else if (pressed & (1u << BTN_DOWN)){
        if(stick_pos_y<4){
            stick_pos_y+=1;
        }
    }
    /*
    draw_key(15, 28, 10, pressed & (1u << BTN_UP));
    draw_key(4, 39, 10, pressed & (1u << BTN_LEFT));
    draw_key(15, 39, 10, pressed & (1u << BTN_CENTER));
    draw_key(26, 39, 10, pressed & (1u << BTN_RIGHT));
    draw_key(15, 50, 10, pressed & (1u << BTN_DOWN));
    */
    /*
    display_text(50, 29, "KEYS", false);
    for (int i = 0; i < 4; i++) {
        bool on = pressed & (1u << (BTN_KEY1 + i));
        int x = 50 + i * 15;
        draw_key(x, 40, 13, on);
        char num[2] = {'1' + i, 0};
        display_text(x + 4, 43, num, on);
    }*/
}

void app_main(void)
{
    ESP_LOGI(TAG, "Hackathon board demo starting");

    bool display_ok = display_init() == ESP_OK;
    if (!display_ok) {
        ESP_LOGE(TAG, "display init failed - continuing without screen");
    }

    ESP_ERROR_CHECK(buttons_init());

    if (display_ok) {
        display_clear();
        /*
        display_text(10, 8, "Hello, Hackathon!", false);
        display_text(10, 24, "ESP32-S3 dev board", false);
        display_text(10, 40, "Self test...", false);
        */
        display_flush();
    }

    bool audio_ok = audio_init() == ESP_OK;
    if (audio_ok) {
        const int notes[] = {523, 659, 784};
        for (int i = 0; i < 3; i++) {
            audio_play_tone(notes[i], 120);
        }
    } else {
        ESP_LOGE(TAG, "audio init failed - continuing without sound");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    QueueHandle_t events = buttons_event_queue();
    int log_div = 0;
    while (1) {
        button_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdTRUE) {
            ESP_LOGI(TAG, "button %s %s", buttons_name(ev.id), ev.pressed ? "pressed" : "released");
            if (ev.pressed && audio_ok) {
                audio_play_tone(s_button_freq[ev.id], 100);
            }
        }

        int mic_level = audio_ok ? audio_mic_level() : 0;
        if (display_ok) {
            draw_ui(buttons_read(), mic_level, audio_ok);
            display_flush();
        }

        if (audio_ok && ++log_div >= 1000 / UI_PERIOD_MS) {
            log_div = 0;
            ESP_LOGI(TAG, "mic level: %d", mic_level);
        }
    }
}

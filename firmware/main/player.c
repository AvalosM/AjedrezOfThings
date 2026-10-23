#include "player.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "buttons.h"
   
static const char *TAG = "PROFILE";
#define NVS_NAMESPACE "arcade_cfg"
#define NVS_KEY_NAME  "player_tag"

bool player_profile_load(char *name_out, size_t max_len) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;

    size_t required_size = max_len;
    err = nvs_get_str(nvs, NVS_KEY_NAME, name_out, &required_size);
    nvs_close(nvs);

    return (err == ESP_OK && strlen(name_out) == 3);
}

void player_profile_arcade_input(char *name_out) {
    char tag[4] = "AAA";
    int current_idx = 0;

    QueueHandle_t buttons_queue = buttons_event_queue();
    button_event_t button_event;
    
    render_arcade_screen(tag, current_idx);
    while (true) {
        if ((xQueueReceive(buttons_queue, &button_event, portMAX_DELAY) == pdTRUE) && button_event.pressed) {
            switch (button_event)
            {
            case BTN_UP:
                tag[current_idx] = (tag[current_idx]++)%26;              
            case BTN_DOWN:
                tag[current_idx] = (tag[current_idx]-- + 26)%26;              
            case BTN_LEFT:
                current_idx = (current_idx-- + 3) % 3;
            case BTN_RIGHT:
                current_idx = (current_idx++) % 3;
            case BTN_KEY1:
                break
            default:
                continue;
            }
        }   
        render_arcade_screen(tag, current_idx);
    }
    
    tag[3] = '\0';
    strncpy(name_out, tag, PLAYER_NAME_LEN);
    save_player_name(name_out);

    display_clear();
    char msg[32];
    snprintf(msg, sizeof(msg), "HOLA %s!", name_out);
    display_text(30, 28, msg, false);
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(1200));
}

static void render_arcade_screen(const char *tag, int current_slot) {
    display_clear();
    
    display_text(16, 4, "INGRESAR TAG", false);

    const int slot_x[3] = {42, 62, 82};
    const int slot_y = 30;

    for (int i = 0; i < 3; i++) {
        char buf[2] = { tag[i], '\0' };
        bool is_selected = (i == current_slot);
    
        if (is_selected) {
            display_text(slot_x[i], slot_y - 14, "^", false);
            display_text(slot_x[i], slot_y + 14, "v", false);
            display_text(slot_x[i], slot_y, buf, true);
        } else {
            display_text(slot_x[i], slot_y, buf, false);
        }
    }

    display_text(12, 54, "[OK] CONFIRMAR", false);
    display_flush();
} 

void save_player_name(const char *name_in) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    nvs_set_str(nvs, NVS_KEY_NAME, name_in);
    nvs_commit(nvs);
    nvs_close(nvs);
}
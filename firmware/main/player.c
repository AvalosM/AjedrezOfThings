#include "player.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "buttons.h"
#include "display.h"
#include "screens.h"

static const char *TAG = "PROFILE";
#define NVS_NAMESPACE "arcade_cfg"
#define NVS_KEY_NAME  "player_tag"

bool player_profile_load(char *name_out, size_t max_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;

    size_t required_size = max_len;
    err = nvs_get_str(nvs, NVS_KEY_NAME, name_out, &required_size);
    nvs_close(nvs);

    if (err == ESP_OK && strlen(name_out) == 3) {
        ESP_LOGI(TAG, "Tag loaded: %s", name_out);
        return true;
    }
    return false;
}

bool load_player(char *name_out, size_t max_len)
{
    return player_profile_load(name_out, max_len);
}

void save_player_name(const char *name_in)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;

    nvs_set_str(nvs, NVS_KEY_NAME, name_in);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Tag saved: %s", name_in);
}

static void render_arcade_screen(const char *tag, int current_slot)
{
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

    display_text(12, 54, "[KEY1] CONFIRMAR", false);
    display_flush();
}

void player_profile_arcade_input(char *name_out)
{
    char tag[4] = "AAA";
    int current_idx = 0;

    if (name_out && strlen(name_out) == 3) {
        memcpy(tag, name_out, 3);
        tag[3] = '\0';
    }

    QueueHandle_t buttons_queue = buttons_event_queue();
    button_event_t button_event;

    render_arcade_screen(tag, current_idx);
    bool confirmed = false;
    while (!confirmed) {
        if ((xQueueReceive(buttons_queue, &button_event, portMAX_DELAY) == pdTRUE) && button_event.pressed) {
            switch (button_event.id) {
            case BTN_RIGHT:
                tag[current_idx] = ((tag[current_idx] - 'A' + 1) % 26) + 'A';
                break;
            case BTN_LEFT:
                tag[current_idx] = ((tag[current_idx] - 'A' + 25) % 26) + 'A';
                break;
            case BTN_UP: 
                current_idx = (current_idx + 2) % 3;
                break;
            case BTN_DOWN:  
                current_idx = (current_idx + 1) % 3;
                break;
            case BTN_KEY1:
            case BTN_KEY2:
            case BTN_KEY3:
            case BTN_KEY4:
            case BTN_CENTER:
                confirmed = true;
                break;
            default:
                break;
            }
            render_arcade_screen(tag, current_idx);
        }
    }

    tag[3] = '\0';
    strncpy(name_out, tag, PLAYER_NAME_LEN);
    name_out[PLAYER_NAME_LEN - 1] = '\0';
    save_player_name(name_out);

    display_clear();
    char msg[32];
    snprintf(msg, sizeof(msg), "HOLA %s!", name_out);
    display_text(30, 28, msg, false);
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(1200));
}

bool player_welcome_screen(const char *name, uint32_t timeout_ms)
{
    screen_welcome(name);

    QueueHandle_t buttons_queue = buttons_event_queue();
    button_event_t ev;

    TickType_t start = xTaskGetTickCount();
    TickType_t wait_ticks = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < wait_ticks) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t remaining = (elapsed < wait_ticks) ? (wait_ticks - elapsed) : 0;

        if (xQueueReceive(buttons_queue, &ev, remaining) == pdTRUE && ev.pressed) {
            if (ev.id == BTN_KEY2) {
                return true;
            } else if (ev.id == BTN_KEY1 || ev.id == BTN_CENTER) {
                return false;
            }
        }
    }

    return false;
}


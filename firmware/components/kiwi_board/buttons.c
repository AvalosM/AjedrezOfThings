#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board.h"
#include "buttons.h"

static const char *TAG = "buttons";

// Usually a low-pass filter is used to debounce button bouncing;
// since we don't have that in hardware, we wait briefly in software.
// This can be improved.
#define DEBOUNCE_MS     20
#define EVENT_QUEUE_LEN 16

// See comments in board.h
// Reference: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html
static const struct {
    gpio_num_t gpio;
    const char *name;
} s_buttons[BTN_COUNT] = {
    [BTN_UP] = {BOARD_KEY_UP_IO, "UP"},
    [BTN_DOWN] = {BOARD_KEY_DOWN_IO, "DOWN"},
    [BTN_LEFT] = {BOARD_KEY_LEFT_IO, "LEFT"},
    [BTN_RIGHT] = {BOARD_KEY_RIGHT_IO, "RIGHT"},
    [BTN_CENTER] = {BOARD_KEY_CENTER_IO, "CENTER"},
    [BTN_KEY1] = {BOARD_KEY_1_IO, "KEY1"},
    [BTN_KEY2] = {BOARD_KEY_2_IO, "KEY2"},
    [BTN_KEY3] = {BOARD_KEY_3_IO, "KEY3"},
    [BTN_KEY4] = {BOARD_KEY_4_IO, "KEY4"},
};

static QueueHandle_t s_isr_queue;
static QueueHandle_t s_event_queue;

static volatile uint32_t s_state;

static inline bool level_is_pressed(button_id_t id)
{
    return gpio_get_level(s_buttons[id].gpio) == 0;
}

/*
    An ISR is an interrupt service routine called when an interrupt occurs.
    For obvious reasons we want to handle the interrupt as quickly as possible,
    and there are specific functions to enqueue from within an ISR.
    https://www.freertos.org/Documentation/02-Kernel/04-API-references/06-Queues/04-xQueueSendFromISR
*/
static void IRAM_ATTR button_isr(void *arg)
{
    button_id_t id = (button_id_t)(uintptr_t)arg;
    BaseType_t woken = pdFALSE;

    gpio_intr_disable(s_buttons[id].gpio);
    xQueueSendFromISR(s_isr_queue, &id, &woken);

    // woken indicates whether writing to the queue unblocked a task of higher
    // priority than the interrupted task, avoiding priority inversion.
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

/*
    Tasks run concurrently, sharing the same address space.
*/
static void buttons_task(void *arg)
{
    button_id_t id;

    while (1) {
        xQueueReceive(s_isr_queue, &id, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

        gpio_intr_enable(s_buttons[id].gpio);

        const uint32_t bit = 1u << id;
        bool pressed = level_is_pressed(id);
        if (pressed == !!(s_state & bit)) {
            continue;
        }
        s_state = pressed ? (s_state | bit) : (s_state & ~bit);

        const button_event_t ev = {.id = id, .pressed = pressed};
        xQueueSend(s_event_queue, &ev, 0);
    }
}

/*
    Button setup:
    An ISR queue that the task reads from,
    an event queue where the rest of the application reads button events,
    and the button handler task.
    https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html#_CPPv411xTaskCreate14TaskFunction_tPCKcK22configSTACK_DEPTH_TYPEPCv11UBaseType_tPC12TaskHandle_t
*/
esp_err_t buttons_init(void)
{
    s_isr_queue = xQueueCreate(BTN_COUNT, sizeof(button_id_t));
    s_event_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(button_event_t));
    ESP_RETURN_ON_FALSE(s_isr_queue && s_event_queue, ESP_ERR_NO_MEM, TAG, "no mem for queues");

    ESP_RETURN_ON_FALSE(xTaskCreate(buttons_task, "buttons", 3072, NULL, 10, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "task create failed");

    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service install failed");

    for (int i = 0; i < BTN_COUNT; i++) {
        const gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_buttons[i].gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "config of %s failed", s_buttons[i].name);
        // For each button, register the button ISR handler.
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_buttons[i].gpio, button_isr, (void *)(uintptr_t)i), TAG,
                            "isr add of %s failed", s_buttons[i].name);
    }

    for (int i = 0; i < BTN_COUNT; i++) {
        if (level_is_pressed(i)) {
            s_state |= 1u << i;
        }
    }
    return ESP_OK;
}

QueueHandle_t buttons_event_queue(void)
{
    return s_event_queue;
}

uint32_t buttons_read(void)
{
    return s_state;
}

const char *buttons_name(button_id_t id)
{
    return id < BTN_COUNT ? s_buttons[id].name : "?";
}

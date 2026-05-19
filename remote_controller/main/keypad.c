#include "keypad.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NUM_ROWS 4
#define NUM_COLS 4
#define KEYPAD_DEBOUNCE_COUNT 2

static const gpio_num_t row_pins[NUM_ROWS] = {GPIO_NUM_19, GPIO_NUM_18, GPIO_NUM_5, GPIO_NUM_17};
static const gpio_num_t col_pins[NUM_COLS] = {GPIO_NUM_16, GPIO_NUM_4, GPIO_NUM_2, GPIO_NUM_15};

static const char key_map[NUM_ROWS][NUM_COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

static char s_last_raw_key = '\0';
static char s_last_reported_key = '\0';
static uint8_t s_stable_count = 0;

void keypad_init(void) {
    // Configure row pins as inputs with pull-ups
    for (int r = 0; r < NUM_ROWS; r++) {
        gpio_config_t rcfg = {
            .pin_bit_mask = (1ULL << row_pins[r]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rcfg);
    }

    // Configure column pins as outputs, initialized to High (1)
    for (int c = 0; c < NUM_COLS; c++) {
        gpio_config_t ccfg = {
            .pin_bit_mask = (1ULL << col_pins[c]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&ccfg);
        gpio_set_level(col_pins[c], 1);
    }

    s_last_raw_key = '\0';
    s_last_reported_key = '\0';
    s_stable_count = 0;
}

char keypad_get_key(void) {
    char detected_key = '\0';

    for (int c = 0; c < NUM_COLS; c++) {
        gpio_set_level(col_pins[c], 0);
        ets_delay_us(25);

        for (int r = 0; r < NUM_ROWS; r++) {
            if (gpio_get_level(row_pins[r]) == 0) {
                detected_key = key_map[r][c];
                break;
            }
        }

        gpio_set_level(col_pins[c], 1);
        if (detected_key != '\0') {
            break;
        }
    }

    if (detected_key == s_last_raw_key) {
        if (s_stable_count < 0xFF) {
            s_stable_count++;
        }
    } else {
        s_last_raw_key = detected_key;
        s_stable_count = detected_key == '\0' ? 0 : 1;
    }

    if (detected_key == '\0') {
        s_last_reported_key = '\0';
        return '\0';
    }

    if (s_stable_count < KEYPAD_DEBOUNCE_COUNT) {
        return '\0';
    }

    if (detected_key == s_last_reported_key) {
        return '\0';
    }

    s_last_reported_key = detected_key;
    return detected_key;
}

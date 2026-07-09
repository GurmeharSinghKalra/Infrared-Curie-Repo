#include "keypad.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NUM_ROWS 4
#define NUM_COLS 4

static const gpio_num_t row_pins[NUM_ROWS] = {GPIO_NUM_19, GPIO_NUM_18, GPIO_NUM_5, GPIO_NUM_17};
static const gpio_num_t col_pins[NUM_COLS] = {GPIO_NUM_16, GPIO_NUM_4, GPIO_NUM_2, GPIO_NUM_15};

static const char key_map[NUM_ROWS][NUM_COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

static bool prev_state[NUM_ROWS][NUM_COLS];

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

    // Initialize state
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            prev_state[r][c] = false;
        }
    }
}

char keypad_get_key(void) {
    char detected_key = '\0';

    for (int c = 0; c < NUM_COLS; c++) {
        // Set column pin low to scan it
        gpio_set_level(col_pins[c], 0);
        ets_delay_us(10); // line stabilization delay

        for (int r = 0; r < NUM_ROWS; r++) {
            int val = gpio_get_level(row_pins[r]);
            bool pressed = (val == 0); // Active Low

            if (pressed && !prev_state[r][c]) {
                // Key transition: not pressed -> pressed
                detected_key = key_map[r][c];
            }
            prev_state[r][c] = pressed;
        }

        // Set column back to High
        gpio_set_level(col_pins[c], 1);
    }

    return detected_key;
}

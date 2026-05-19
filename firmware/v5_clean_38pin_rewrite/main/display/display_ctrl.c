#include "display_ctrl.h"
#include "state/robot_state.h"
#include "drivers/max7219_bsp.h"
#include "u8g2.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/i2c.h"
#include <string.h>

static const char *TAG = "DISPLAY";

// =====================================================================
//  PIN DEFINITIONS (from V1)
// =====================================================================

#define I2C_PORT_0 0
#define LEFT_OLED_SDA  21
#define LEFT_OLED_SCL  22

#define I2C_PORT_1 1
#define RIGHT_OLED_SDA 32
#define RIGHT_OLED_SCL 33

#define BLINK_MIN_TICKS   60
#define BLINK_MAX_TICKS   120
#define BLINK_DURATION_MS 120

static u8g2_t u8g2_left;
static u8g2_t u8g2_right;

// =====================================================================
//  I2C / U8G2 SETUP (from V1)
// =====================================================================

static void init_i2c_ports(void) {
    i2c_config_t conf0 = {
        .mode = I2C_MODE_MASTER, .sda_io_num = LEFT_OLED_SDA, .scl_io_num = LEFT_OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE, .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT_0, &conf0));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT_0, conf0.mode, 0, 0, 0));

    i2c_config_t conf1 = {
        .mode = I2C_MODE_MASTER, .sda_io_num = RIGHT_OLED_SDA, .scl_io_num = RIGHT_OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE, .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT_1, &conf1));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT_1, conf1.mode, 0, 0, 0));
}

#define I2C_BUFFER_SIZE 1024
static uint8_t u8g2_esp32_i2c_byte_cb_left(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[I2C_BUFFER_SIZE]; static uint16_t buf_idx = 0;
    switch(msg) {
        case U8X8_MSG_BYTE_SEND: 
            if (buf_idx + arg_int <= I2C_BUFFER_SIZE) { memcpy(&buffer[buf_idx], arg_ptr, arg_int); buf_idx += arg_int; }
            break;
        case U8X8_MSG_BYTE_START_TRANSFER: buf_idx = 0; break;
        case U8X8_MSG_BYTE_END_TRANSFER: i2c_master_write_to_device(I2C_PORT_0, u8x8_GetI2CAddress(u8x8)>>1, buffer, buf_idx, pdMS_TO_TICKS(100)); break;
    }
    return 1;
}

static uint8_t u8g2_esp32_i2c_byte_cb_right(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[I2C_BUFFER_SIZE]; static uint16_t buf_idx = 0;
    switch(msg) {
        case U8X8_MSG_BYTE_SEND: 
            if (buf_idx + arg_int <= I2C_BUFFER_SIZE) { memcpy(&buffer[buf_idx], arg_ptr, arg_int); buf_idx += arg_int; }
            break;
        case U8X8_MSG_BYTE_START_TRANSFER: buf_idx = 0; break;
        case U8X8_MSG_BYTE_END_TRANSFER: i2c_master_write_to_device(I2C_PORT_1, u8x8_GetI2CAddress(u8x8)>>1, buffer, buf_idx, pdMS_TO_TICKS(100)); break;
    }
    return 1;
}

static uint8_t u8g2_esp32_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    if (msg == U8X8_MSG_DELAY_MILLI) vTaskDelay(pdMS_TO_TICKS(arg_int)); 
    return 1;
}

// =====================================================================
//  DRAWING ROUTINES (from V1)
// =====================================================================

static void draw_blink(u8g2_t *u8g2) {
    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawRBox(u8g2, 20, 28, 88, 8, 4);
    u8g2_SendBuffer(u8g2);
}

static void draw_eye(u8g2_t *u8g2, robot_expression_t exp, bool is_left) {
    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);
    switch(exp) {
        case EXP_HAPPY:
            // ◠‿◠ — Large round eye with curved smile underneath
            u8g2_DrawDisc(u8g2, 64, 28, 22, U8G2_DRAW_ALL);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawDisc(u8g2, 64, 28, 12, U8G2_DRAW_ALL);        // Hollow center
            u8g2_DrawBox(u8g2, 30, 0, 68, 28);                      // Cut top half to make ◠ shape
            break;

        case EXP_NEUTRAL:
            // •_• — Simple centered dot
            u8g2_DrawDisc(u8g2, 64, 30, 10, U8G2_DRAW_ALL);
            break;

        case EXP_SAD:
            // ╥﹏╥ — Droopy eye with tear-line brow
            u8g2_DrawRBox(u8g2, 34, 24, 60, 22, 8);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawDisc(u8g2, 64, 35, 6, U8G2_DRAW_ALL);        // Pupil
            u8g2_SetDrawColor(u8g2, 1);
            // Angled brow (drooping outward)
            if (is_left)
                u8g2_DrawTriangle(u8g2, 30, 22, 98, 14, 98, 22);
            else
                u8g2_DrawTriangle(u8g2, 30, 14, 98, 22, 30, 22);
            break;

        case EXP_WINK:
            if (is_left) {
                // Open eye — round with pupil
                u8g2_DrawDisc(u8g2, 64, 30, 18, U8G2_DRAW_ALL);
                u8g2_SetDrawColor(u8g2, 0);
                u8g2_DrawDisc(u8g2, 64, 30, 8, U8G2_DRAW_ALL);
            } else {
                // Closed eye — horizontal smile line
                u8g2_DrawRBox(u8g2, 30, 28, 68, 6, 3);
            }
            break;

        case EXP_LOVE:
            // ♥‿♥ — Heart shape using two overlapping discs + triangle
            u8g2_DrawDisc(u8g2, 52, 24, 14, U8G2_DRAW_ALL);
            u8g2_DrawDisc(u8g2, 76, 24, 14, U8G2_DRAW_ALL);
            u8g2_DrawTriangle(u8g2, 38, 28, 90, 28, 64, 54);
            break;

        case EXP_ANGRY:
            // >_< — Narrow slit with aggressive angled brow
            u8g2_DrawRBox(u8g2, 38, 28, 52, 10, 3);                // Narrow eye slit
            // Angry brow cutting across
            if (is_left)
                u8g2_DrawTriangle(u8g2, 30, 12, 98, 26, 30, 26);
            else
                u8g2_DrawTriangle(u8g2, 30, 26, 98, 12, 98, 26);
            break;

        case EXP_SLEEP:
            // -_- — Just a thin horizontal line (eyes closed)
            u8g2_DrawRBox(u8g2, 24, 30, 80, 5, 2);
            break;

        case EXP_SCAN:
            // ◉_◉ — Large outer circle with tiny centered pupil
            u8g2_DrawCircle(u8g2, 64, 32, 26, U8G2_DRAW_ALL);
            u8g2_DrawCircle(u8g2, 64, 32, 25, U8G2_DRAW_ALL);     // Thick ring
            u8g2_DrawDisc(u8g2, 64, 32, 5, U8G2_DRAW_ALL);        // Tiny pupil
            break;

        case EXP_SURPRISE:
            u8g2_DrawCircle(u8g2, 64, 32, 24, U8G2_DRAW_ALL);
            u8g2_DrawCircle(u8g2, 64, 32, 23, U8G2_DRAW_ALL);
            u8g2_DrawDisc(u8g2, 64, 32, 8, U8G2_DRAW_ALL);
            break;

        case EXP_CURIOUS:
            u8g2_DrawRBox(u8g2, 34, 24, 60, 22, 9);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawDisc(u8g2, is_left ? 54 : 74, 35, 6, U8G2_DRAW_ALL);
            u8g2_SetDrawColor(u8g2, 1);
            if (is_left) u8g2_DrawLine(u8g2, 34, 18, 92, 10);
            else u8g2_DrawLine(u8g2, 36, 10, 94, 18);
            break;

        case EXP_EXCITED:
            u8g2_DrawTriangle(u8g2, 64, 10, 34, 50, 94, 50);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawDisc(u8g2, 64, 34, 7, U8G2_DRAW_ALL);
            break;

        case EXP_CONFUSED:
            u8g2_DrawRBox(u8g2, 36, 26, 56, 18, 8);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawDisc(u8g2, is_left ? 72 : 56, 34, 5, U8G2_DRAW_ALL);
            u8g2_SetDrawColor(u8g2, 1);
            u8g2_DrawArc(u8g2, 64, 14, 18, 0, 180);
            break;

        case EXP_LOST:
            u8g2_DrawRBox(u8g2, 36, 30, 56, 8, 4);
            u8g2_DrawLine(u8g2, 32, 20, 96, 44);
            u8g2_DrawLine(u8g2, 32, 44, 96, 20);
            break;

        case EXP_CUSTOM:
            // Same as happy for OLED — custom only affects mouth matrix
            u8g2_DrawDisc(u8g2, 64, 28, 22, U8G2_DRAW_ALL);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawDisc(u8g2, 64, 28, 12, U8G2_DRAW_ALL);
            u8g2_DrawBox(u8g2, 30, 0, 68, 28);
            break;

        default:
            u8g2_DrawDisc(u8g2, 64, 30, 10, U8G2_DRAW_ALL);
            break;
    }
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SendBuffer(u8g2);
}

static void draw_mouth(robot_expression_t exp) {
    uint8_t left[8] = {0}, right[8] = {0};
    if (exp == EXP_CUSTOM) {
        uint8_t c[16]; robot_get_custom_mouth(c);
        memcpy(left, c, 8); memcpy(right, c + 8, 8);
    } else if (exp == EXP_HAPPY) {
        // Wide upward smile arc  ◡
        uint8_t l[8] = {0x00, 0x00, 0x00, 0x00, 0x3F, 0x60, 0xC0, 0x00};
        uint8_t r[8] = {0x00, 0x00, 0x00, 0x00, 0xFC, 0x06, 0x03, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_NEUTRAL) {
        // Simple horizontal line  —
        uint8_t l[8] = {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00};
        uint8_t r[8] = {0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_SAD) {
        // Downward frown arc  ◠
        uint8_t l[8] = {0x00, 0x00, 0xC0, 0x60, 0x3F, 0x00, 0x00, 0x00};
        uint8_t r[8] = {0x00, 0x00, 0x03, 0x06, 0xFC, 0x00, 0x00, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_WINK) {
        // Slight asymmetric smirk
        uint8_t l[8] = {0x00, 0x00, 0x00, 0x00, 0x1F, 0x20, 0x40, 0x00};
        uint8_t r[8] = {0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_LOVE) {
        // Heart outline  ♥
        uint8_t l[8] = {0x00, 0x36, 0x49, 0x41, 0x41, 0x22, 0x14, 0x08};
        uint8_t r[8] = {0x00, 0x6C, 0x92, 0x82, 0x82, 0x44, 0x28, 0x10};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_ANGRY) {
        // Zigzag gritted teeth
        uint8_t l[8] = {0x00, 0x00, 0xFF, 0x55, 0xAA, 0xFF, 0x00, 0x00};
        uint8_t r[8] = {0x00, 0x00, 0xFF, 0xAA, 0x55, 0xFF, 0x00, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_SLEEP) {
        // Three dots  z z z
        uint8_t l[8] = {0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00};
        uint8_t r[8] = {0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_SCAN) {
        // Open "O" mouth
        uint8_t l[8] = {0x00, 0x1E, 0x21, 0x21, 0x21, 0x1E, 0x00, 0x00};
        uint8_t r[8] = {0x00, 0x78, 0x84, 0x84, 0x84, 0x78, 0x00, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_SURPRISE) {
        uint8_t l[8] = {0x00, 0x0E, 0x11, 0x21, 0x21, 0x11, 0x0E, 0x00};
        uint8_t r[8] = {0x00, 0x70, 0x88, 0x84, 0x84, 0x88, 0x70, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_CURIOUS) {
        uint8_t l[8] = {0x00, 0x00, 0x18, 0x24, 0x04, 0x08, 0x08, 0x00};
        uint8_t r[8] = {0x00, 0x00, 0x18, 0x24, 0x20, 0x10, 0x10, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_EXCITED) {
        uint8_t l[8] = {0x00, 0x24, 0x66, 0xFF, 0xFF, 0x66, 0x24, 0x00};
        uint8_t r[8] = {0x00, 0x24, 0x66, 0xFF, 0xFF, 0x66, 0x24, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_CONFUSED) {
        uint8_t l[8] = {0x00, 0x20, 0x10, 0x08, 0x10, 0x20, 0x00, 0x00};
        uint8_t r[8] = {0x00, 0x04, 0x08, 0x10, 0x08, 0x04, 0x00, 0x00};
        memcpy(left, l, 8); memcpy(right, r, 8);
    } else if (exp == EXP_LOST) {
        uint8_t l[8] = {0x00, 0x81, 0x42, 0x24, 0x18, 0x24, 0x42, 0x81};
        uint8_t r[8] = {0x00, 0x81, 0x42, 0x24, 0x18, 0x24, 0x42, 0x81};
        memcpy(left, l, 8); memcpy(right, r, 8);
    }
    for(int row=0; row<8; row++) {
        max7219_set_row(0, row, left[7-row]); // Vertical flip preserved from V1
        max7219_set_row(1, row, right[7-row]);
    }
}

// =====================================================================
//  TASK
// =====================================================================

void task_display(void *arg) {
    ESP_LOGI(TAG, "Initializing Displays...");
    init_i2c_ports();
    max7219_init();

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_left, U8G2_R0, u8g2_esp32_i2c_byte_cb_left, u8g2_esp32_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2_left); u8g2_SetPowerSave(&u8g2_left, 0);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_right, U8G2_R0, u8g2_esp32_i2c_byte_cb_right, u8g2_esp32_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2_right); u8g2_SetPowerSave(&u8g2_right, 0);

    robot_expression_t last_exp = -1;
    int last_bright = -1;
    bool was_on = true;

    int blink_counter = 0;
    int next_blink = BLINK_MIN_TICKS + (esp_random() % (BLINK_MAX_TICKS - BLINK_MIN_TICKS));

    while(1) {
        bool is_on = robot_get_power();
        robot_state_t state = robot_state_get();

        if (!is_on) {
            if (was_on) {
                u8g2_ClearBuffer(&u8g2_left); u8g2_SendBuffer(&u8g2_left);
                u8g2_ClearBuffer(&u8g2_right); u8g2_SendBuffer(&u8g2_right);
                max7219_clear(); max7219_shutdown(true);
                was_on = false;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        } else if (!was_on) {
            max7219_shutdown(false);
            last_exp = -1; last_bright = -1;
            was_on = true;
        }

        robot_expression_t cur_exp = robot_get_expression();
        int cur_bright = robot_get_brightness();

        // Override expression based on state (Behavior-linked expressions)
        if (state == ROBOT_STATE_ERROR) {
            cur_exp = EXP_SAD; // Show sad face on error
        } else if (state == ROBOT_STATE_LOW_POWER) {
            cur_bright = 1; // Dim mouth
        }

        bool force_b = robot_get_and_clear_blink();
        blink_counter++;

        // Blinking (faster blinking if IDLE)
        if (state == ROBOT_STATE_IDLE && next_blink > BLINK_MIN_TICKS * 1.5) {
            next_blink = BLINK_MIN_TICKS; // more attentive looking around
        }

        if (blink_counter >= next_blink || force_b) {
            draw_blink(&u8g2_left); draw_blink(&u8g2_right);
            vTaskDelay(pdMS_TO_TICKS(BLINK_DURATION_MS));
            last_exp = -1; // force redraw
            blink_counter = 0;
            next_blink = BLINK_MIN_TICKS + (esp_random() % (BLINK_MAX_TICKS - BLINK_MIN_TICKS));
        }

        if (cur_exp != last_exp) {
            draw_eye(&u8g2_left, cur_exp, true);
            draw_eye(&u8g2_right, cur_exp, false);
            draw_mouth(cur_exp);
            last_exp = cur_exp;
        }

        if (cur_bright != last_bright) {
            max7219_set_intensity(cur_bright);
            last_bright = cur_bright;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

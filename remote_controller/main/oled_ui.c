#include "oled_ui.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2.h"
#include <string.h>

static const char *TAG = "CTRL_OLED";

#define CTRL_OLED_I2C_PORT I2C_NUM_0
#define CTRL_OLED_SDA_GPIO 13
#define CTRL_OLED_SCL_GPIO 14
#define CTRL_OLED_I2C_SPEED_HZ 100000
#define CTRL_OLED_BUF_SIZE 1024
#define ACTION_OVERLAY_US 1200000

static u8g2_t s_u8g2;
static bool s_ui_ready = false;
static char s_action_label[24] = "";
static int64_t s_action_until_us = 0;

static uint8_t oled_i2c_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buf[CTRL_OLED_BUF_SIZE];
    static uint16_t idx = 0;

    switch (msg) {
        case U8X8_MSG_BYTE_SEND:
            if ((idx + arg_int) <= CTRL_OLED_BUF_SIZE) {
                memcpy(&buf[idx], arg_ptr, arg_int);
                idx += arg_int;
            }
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            i2c_master_write_to_device(CTRL_OLED_I2C_PORT,
                                       u8x8_GetI2CAddress(u8x8) >> 1,
                                       buf,
                                       idx,
                                       pdMS_TO_TICKS(100));
            break;
        default:
            break;
    }
    return 1;
}

static uint8_t oled_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)u8x8;
    (void)arg_ptr;
    if (msg == U8X8_MSG_DELAY_MILLI) {
        vTaskDelay(pdMS_TO_TICKS(arg_int));
    }
    return 1;
}

static bool probe_i2c_address(uint8_t addr_7bit) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t rc = i2c_master_start(cmd);
    if (rc == ESP_OK) {
        rc = i2c_master_write_byte(cmd, (addr_7bit << 1) | I2C_MASTER_WRITE, true);
    }
    if (rc == ESP_OK) {
        rc = i2c_master_stop(cmd);
    }
    if (rc == ESP_OK) {
        rc = i2c_master_cmd_begin(CTRL_OLED_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    }
    i2c_cmd_link_delete(cmd);
    return rc == ESP_OK;
}

static uint8_t detect_oled_addr_8bit(void) {
    static const uint8_t candidates[] = {0x3C, 0x3D};
    for (size_t i = 0; i < sizeof(candidates); i++) {
        if (probe_i2c_address(candidates[i])) {
            return (uint8_t)(candidates[i] << 1);
        }
    }
    return 0;
}

static void draw_battery(int percent) {
    const int x = 100;
    const int y = 2;
    const int w = 22;
    const int h = 10;

    u8g2_DrawFrame(&s_u8g2, x, y, w, h);
    u8g2_DrawBox(&s_u8g2, x + w, y + 3, 2, 4);

    if (percent >= 0) {
        int fill = (percent * (w - 4)) / 100;
        if (fill < 1 && percent > 0) fill = 1;
        if (fill > 0) {
            u8g2_DrawBox(&s_u8g2, x + 2, y + 2, fill, h - 4);
        }
    } else {
        u8g2_SetFont(&s_u8g2, u8g2_font_4x6_tf);
        u8g2_DrawStr(&s_u8g2, x + 3, y + 8, "PWR");
    }
}

static void draw_link_icon(bool linked, bool connecting) {
    if (linked) {
        u8g2_DrawDisc(&s_u8g2, 8, 7, 2, U8G2_DRAW_ALL);
        u8g2_DrawDisc(&s_u8g2, 18, 7, 2, U8G2_DRAW_ALL);
        u8g2_DrawLine(&s_u8g2, 10, 7, 16, 7);
    } else if (connecting) {
        int phase = (int)((esp_timer_get_time() / 200000) % 3);
        u8g2_DrawCircle(&s_u8g2, 8, 7, 2, U8G2_DRAW_ALL);
        u8g2_DrawCircle(&s_u8g2, 18, 7, 2, U8G2_DRAW_ALL);
        for (int i = 0; i <= phase; i++) {
            u8g2_DrawDisc(&s_u8g2, 11 + (i * 3), 7, 1, U8G2_DRAW_ALL);
        }
    } else {
        u8g2_DrawCircle(&s_u8g2, 8, 7, 2, U8G2_DRAW_ALL);
        u8g2_DrawCircle(&s_u8g2, 18, 7, 2, U8G2_DRAW_ALL);
        u8g2_DrawLine(&s_u8g2, 10, 5, 16, 9);
    }
}

static void draw_searching_screen(bool connecting, int battery_percent) {
    int dots = (int)((esp_timer_get_time() / 300000) % 4);
    char line[24];

    u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);
    draw_link_icon(false, connecting);
    draw_battery(battery_percent);
    u8g2_DrawStr(&s_u8g2, 28, 10, connecting ? "Connecting" : "Finding robot");

    u8g2_DrawRFrame(&s_u8g2, 14, 18, 100, 32, 6);
    u8g2_DrawDisc(&s_u8g2, 42, 31, 6, U8G2_DRAW_ALL);
    u8g2_DrawDisc(&s_u8g2, 86, 31, 6, U8G2_DRAW_ALL);
    u8g2_SetDrawColor(&s_u8g2, 0);
    u8g2_DrawDisc(&s_u8g2, 42, 31, 2, U8G2_DRAW_ALL);
    u8g2_DrawDisc(&s_u8g2, 86, 31, 2, U8G2_DRAW_ALL);
    u8g2_SetDrawColor(&s_u8g2, 1);
    u8g2_DrawLine(&s_u8g2, 49, 39, 79, 39);
    u8g2_DrawLine(&s_u8g2, 50, 40, 78, 40);
    u8g2_DrawLine(&s_u8g2, 52, 41, 76, 41);

    strcpy(line, "Hold on");
    while ((int)strlen(line) < 7 + dots) {
        strcat(line, ".");
    }
    u8g2_SetFont(&s_u8g2, u8g2_font_7x14_tf);
    u8g2_DrawStr(&s_u8g2, 32, 60, line);
}

static void draw_ready_screen(const char *expression_label,
                              const char *speed_label,
                              int battery_percent) {
    draw_link_icon(true, false);
    draw_battery(battery_percent);

    u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&s_u8g2, 28, 10, "Robot ready");

    u8g2_SetFont(&s_u8g2, u8g2_font_logisoso20_tf);
    int expr_w = u8g2_GetStrWidth(&s_u8g2, expression_label);
    u8g2_DrawStr(&s_u8g2, (128 - expr_w) / 2, 39, expression_label);

    u8g2_SetFont(&s_u8g2, u8g2_font_7x14_tf);
    int speed_w = u8g2_GetStrWidth(&s_u8g2, speed_label);
    u8g2_DrawRFrame(&s_u8g2, 16, 44, 96, 18, 4);
    u8g2_DrawStr(&s_u8g2, (128 - speed_w) / 2, 57, speed_label);
}

static void draw_action_overlay(const char *label) {
    u8g2_DrawRFrame(&s_u8g2, 6, 14, 116, 36, 6);
    u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&s_u8g2, 36, 24, "Nice move!");
    u8g2_SetFont(&s_u8g2, u8g2_font_9x18B_tf);
    int w = u8g2_GetStrWidth(&s_u8g2, label);
    u8g2_DrawStr(&s_u8g2, (128 - w) / 2, 44, label);
}

void controller_ui_init(void) {
    if (s_ui_ready) {
        return;
    }

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CTRL_OLED_SDA_GPIO,
        .scl_io_num = CTRL_OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CTRL_OLED_I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(CTRL_OLED_I2C_PORT, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(CTRL_OLED_I2C_PORT, cfg.mode, 0, 0, 0));

    uint8_t oled_addr = detect_oled_addr_8bit();
    if (oled_addr == 0) {
        ESP_LOGW(TAG, "No OLED detected on SDA=%d SCL=%d (tried 0x3C and 0x3D)",
                 CTRL_OLED_SDA_GPIO, CTRL_OLED_SCL_GPIO);
        return;
    }

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&s_u8g2, U8G2_R0, oled_i2c_cb, oled_gpio_and_delay_cb);
    u8g2_SetI2CAddress(&s_u8g2, oled_addr);
    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    s_ui_ready = true;

    ESP_LOGI(TAG, "Controller OLED ready on SDA=%d SCL=%d addr=0x%02X",
             CTRL_OLED_SDA_GPIO, CTRL_OLED_SCL_GPIO, oled_addr >> 1);
}

void controller_ui_show_action(const char *label) {
    if (!label || !label[0]) {
        return;
    }
    strncpy(s_action_label, label, sizeof(s_action_label) - 1);
    s_action_label[sizeof(s_action_label) - 1] = '\0';
    s_action_until_us = esp_timer_get_time() + ACTION_OVERLAY_US;
}

void controller_ui_render(bool connected,
                          bool connecting,
                          const char *expression_label,
                          const char *speed_label,
                          int battery_percent) {
    if (!s_ui_ready) {
        return;
    }

    u8g2_ClearBuffer(&s_u8g2);

    if (!connected) {
        draw_searching_screen(connecting, battery_percent);
    } else {
        const char *expr = (expression_label && expression_label[0]) ? expression_label : "READY";
        const char *spd = (speed_label && speed_label[0]) ? speed_label : "Play";
        draw_ready_screen(expr, spd, battery_percent);

        if (battery_percent >= 0 && battery_percent <= 20) {
            u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);
            u8g2_DrawStr(&s_u8g2, 8, 62, "Battery low");
        }
    }

    if (connected && esp_timer_get_time() < s_action_until_us) {
        draw_action_overlay(s_action_label);
    }

    u8g2_SendBuffer(&s_u8g2);
}

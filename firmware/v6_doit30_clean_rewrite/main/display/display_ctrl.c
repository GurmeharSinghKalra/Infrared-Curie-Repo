#include "display_ctrl.h"
#include "board/board_config.h"
#include "state/robot_state.h"
#include "drivers/max7219_bsp.h"
#include "u8g2.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/i2c.h"
#include <string.h>

static const char *TAG = "DISPLAY";

#define BLINK_MIN_TICKS   60
#define BLINK_MAX_TICKS   120
#define BLINK_DURATION_MS 120

#define EYE_CENTER_X 64
#define EYE_CENTER_Y 31
#define EYE_BASE_X   24
#define EYE_OUTER_X  28
#define EYE_INNER_X  100
#define BROW_THICK   3

typedef enum {
    EYE_STYLE_OPEN,
    EYE_STYLE_WIDE,
    EYE_STYLE_SQUINT,
    EYE_STYLE_CLOSED,
} eye_style_t;

typedef struct {
    eye_style_t style;
    uint8_t width;
    uint8_t height;
    int8_t pupil_dx;
    int8_t pupil_dy;
    uint8_t pupil_radius;
    int8_t brow_outer_y;
    int8_t brow_inner_y;
} eye_profile_t;

typedef struct {
    eye_profile_t left;
    eye_profile_t right;
    const uint16_t (*mouth_frames)[8];
    uint8_t mouth_frame_count;
    uint8_t mouth_frame_period;
} face_profile_t;

static u8g2_t u8g2_left;
static u8g2_t u8g2_right;

static uint8_t reverse_bits8(uint8_t value) {
    value = ((value & 0xF0) >> 4) | ((value & 0x0F) << 4);
    value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2);
    value = ((value & 0xAA) >> 1) | ((value & 0x55) << 1);
    return value;
}

static const uint16_t MOUTH_NEUTRAL[][8] = {
    {0x0000, 0x0000, 0x0000, 0x0000, 0x7FFE, 0x0000, 0x0000, 0x0000},
};

static const uint16_t MOUTH_HAPPY[][8] = {
    {0x0000, 0x0000, 0x0000, 0x03C0, 0x0C30, 0x1008, 0x2004, 0x1FF8},
    {0x0000, 0x0000, 0x03C0, 0x0C30, 0x1008, 0x2004, 0x1FF8, 0x0000},
};

static const uint16_t MOUTH_SAD[][8] = {
    {0x1FF8, 0x2004, 0x1008, 0x0C30, 0x03C0, 0x0000, 0x0000, 0x0000},
    {0x0FF0, 0x1008, 0x0810, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000},
};

static const uint16_t MOUTH_ANGRY[][8] = {
    {0x0000, 0x7FFE, 0x5554, 0x2AA8, 0x5554, 0x7FFE, 0x0000, 0x0000},
    {0x0000, 0x3FFC, 0x6DB6, 0x9248, 0x6DB6, 0x3FFC, 0x0000, 0x0000},
};

static const uint16_t MOUTH_FEAR[][8] = {
    {0x03C0, 0x0C30, 0x1808, 0x1008, 0x1008, 0x1808, 0x0C30, 0x03C0},
    {0x0180, 0x03C0, 0x0C30, 0x1808, 0x1808, 0x0C30, 0x03C0, 0x0180},
};

static const uint16_t MOUTH_DISGUST[][8] = {
    {0x0000, 0x0000, 0x1FE0, 0x2010, 0x0C08, 0x0604, 0x0202, 0x0000},
    {0x0000, 0x0000, 0x3FC0, 0x0820, 0x0610, 0x0308, 0x0104, 0x0000},
};

static const uint16_t MOUTH_CONFUSED[][8] = {
    {0x0000, 0x0200, 0x0500, 0x0880, 0x1100, 0x2080, 0x4040, 0x0000},
    {0x0000, 0x0040, 0x0080, 0x1100, 0x0880, 0x0500, 0x0200, 0x0000},
};

static const uint16_t MOUTH_CONTEMPT[][8] = {
    {0x0000, 0x0000, 0x0FF8, 0x1004, 0x3002, 0x6002, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x07FC, 0x1802, 0x2002, 0x4002, 0x0000, 0x0000},
};

static const uint16_t MOUTH_THOUGHTFUL[][8] = {
    {0x0000, 0x0000, 0x0000, 0x1FF0, 0x0008, 0x0010, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000, 0x0FF8, 0x0010, 0x0008, 0x0000, 0x0000},
};

static const uint16_t MOUTH_SHY[][8] = {
    {0x0000, 0x0000, 0x0000, 0x03C0, 0x0C30, 0x0808, 0x07F0, 0x0000},
    {0x0000, 0x0000, 0x0000, 0x01E0, 0x0618, 0x0408, 0x03F0, 0x0000},
};

static const uint16_t MOUTH_FUNNY[][8] = {
    {0x0000, 0x0000, 0x1FF8, 0x2004, 0x3FFC, 0x07E0, 0x0420, 0x0000},
    {0x0000, 0x0000, 0x1FF8, 0x2004, 0x3FFC, 0x03C0, 0x0240, 0x0000},
};

static const uint16_t MOUTH_SURPRISED[][8] = {
    {0x07E0, 0x1808, 0x2004, 0x4002, 0x4002, 0x2004, 0x1808, 0x07E0},
    {0x03C0, 0x0FF0, 0x1808, 0x300C, 0x300C, 0x1808, 0x0FF0, 0x03C0},
};

static const uint16_t MOUTH_EXCITED[][8] = {
    {0x0000, 0x0000, 0x03C0, 0x0FF0, 0x1FF8, 0x300C, 0x3FFC, 0x1FF8},
    {0x0000, 0x03C0, 0x0FF0, 0x1FF8, 0x300C, 0x3FFC, 0x1FF8, 0x0000},
};

static const uint16_t MOUTH_WINK[][8] = {
    {0x0000, 0x0000, 0x00F8, 0x0104, 0x0202, 0x3E02, 0x4002, 0x0000},
};

static const uint16_t MOUTH_LOVE[][8] = {
    {0x0000, 0x0C30, 0x1E78, 0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180},
};

static const uint16_t MOUTH_SLEEP[][8] = {
    {0x0000, 0x3000, 0x1800, 0x0C00, 0x0300, 0x0180, 0x00C0, 0x0000},
    {0x0000, 0x1800, 0x0C00, 0x0300, 0x0180, 0x00C0, 0x0060, 0x0000},
};

static const uint16_t MOUTH_SCAN[][8] = {
    {0x0000, 0x03C0, 0x0C30, 0x1008, 0x1008, 0x0C30, 0x03C0, 0x0000},
    {0x0000, 0x0000, 0x03C0, 0x0C30, 0x1008, 0x0C30, 0x03C0, 0x0000},
};

static const uint16_t MOUTH_LOST[][8] = {
    {0x0000, 0x4002, 0x2404, 0x1808, 0x1818, 0x2424, 0x4242, 0x0000},
};

static const face_profile_t FACE_PROFILES[EXP_COUNT] = {
    [EXP_NEUTRAL] = {
        .left =  {.style = EYE_STYLE_OPEN,  .width = 72, .height = 20, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 7, .brow_outer_y = 18, .brow_inner_y = 18},
        .right = {.style = EYE_STYLE_OPEN,  .width = 72, .height = 20, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 7, .brow_outer_y = 18, .brow_inner_y = 18},
        .mouth_frames = MOUTH_NEUTRAL, .mouth_frame_count = 1, .mouth_frame_period = 1,
    },
    [EXP_HAPPY] = {
        .left =  {.style = EYE_STYLE_OPEN,  .width = 76, .height = 22, .pupil_dx = 0,  .pupil_dy = -1, .pupil_radius = 7, .brow_outer_y = 17, .brow_inner_y = 20},
        .right = {.style = EYE_STYLE_OPEN,  .width = 76, .height = 22, .pupil_dx = 0,  .pupil_dy = -1, .pupil_radius = 7, .brow_outer_y = 17, .brow_inner_y = 20},
        .mouth_frames = MOUTH_HAPPY, .mouth_frame_count = 2, .mouth_frame_period = 12,
    },
    [EXP_SAD] = {
        .left =  {.style = EYE_STYLE_OPEN,  .width = 72, .height = 18, .pupil_dx = -1, .pupil_dy = 3,  .pupil_radius = 6, .brow_outer_y = 15, .brow_inner_y = 11},
        .right = {.style = EYE_STYLE_OPEN,  .width = 72, .height = 18, .pupil_dx = 1,  .pupil_dy = 3,  .pupil_radius = 6, .brow_outer_y = 15, .brow_inner_y = 11},
        .mouth_frames = MOUTH_SAD, .mouth_frame_count = 2, .mouth_frame_period = 14,
    },
    [EXP_ANGRY] = {
        .left =  {.style = EYE_STYLE_SQUINT, .width = 76, .height = 10, .pupil_dx = 2,  .pupil_dy = 0,  .pupil_radius = 5, .brow_outer_y = 19, .brow_inner_y = 10},
        .right = {.style = EYE_STYLE_SQUINT, .width = 76, .height = 10, .pupil_dx = -2, .pupil_dy = 0,  .pupil_radius = 5, .brow_outer_y = 19, .brow_inner_y = 10},
        .mouth_frames = MOUTH_ANGRY, .mouth_frame_count = 2, .mouth_frame_period = 10,
    },
    [EXP_FEAR] = {
        .left =  {.style = EYE_STYLE_WIDE,  .width = 74, .height = 28, .pupil_dx = 0,  .pupil_dy = -2, .pupil_radius = 6, .brow_outer_y = 14, .brow_inner_y = 11},
        .right = {.style = EYE_STYLE_WIDE,  .width = 74, .height = 28, .pupil_dx = 0,  .pupil_dy = -2, .pupil_radius = 6, .brow_outer_y = 14, .brow_inner_y = 11},
        .mouth_frames = MOUTH_FEAR, .mouth_frame_count = 2, .mouth_frame_period = 8,
    },
    [EXP_DISGUST] = {
        .left =  {.style = EYE_STYLE_SQUINT, .width = 72, .height = 12, .pupil_dx = 4,  .pupil_dy = 0,  .pupil_radius = 5, .brow_outer_y = 18, .brow_inner_y = 15},
        .right = {.style = EYE_STYLE_OPEN,   .width = 68, .height = 16, .pupil_dx = -2, .pupil_dy = 1,  .pupil_radius = 5, .brow_outer_y = 17, .brow_inner_y = 19},
        .mouth_frames = MOUTH_DISGUST, .mouth_frame_count = 2, .mouth_frame_period = 12,
    },
    [EXP_CONFUSED] = {
        .left =  {.style = EYE_STYLE_OPEN,   .width = 70, .height = 17, .pupil_dx = 3,  .pupil_dy = 1,  .pupil_radius = 6, .brow_outer_y = 15, .brow_inner_y = 21},
        .right = {.style = EYE_STYLE_OPEN,   .width = 70, .height = 20, .pupil_dx = -4, .pupil_dy = -1, .pupil_radius = 6, .brow_outer_y = 21, .brow_inner_y = 13},
        .mouth_frames = MOUTH_CONFUSED, .mouth_frame_count = 2, .mouth_frame_period = 14,
    },
    [EXP_CONTEMPT] = {
        .left =  {.style = EYE_STYLE_SQUINT, .width = 72, .height = 12, .pupil_dx = 2,  .pupil_dy = 0,  .pupil_radius = 5, .brow_outer_y = 17, .brow_inner_y = 18},
        .right = {.style = EYE_STYLE_OPEN,   .width = 68, .height = 16, .pupil_dx = -3, .pupil_dy = 1,  .pupil_radius = 5, .brow_outer_y = 15, .brow_inner_y = 20},
        .mouth_frames = MOUTH_CONTEMPT, .mouth_frame_count = 2, .mouth_frame_period = 16,
    },
    [EXP_THOUGHTFUL] = {
        .left =  {.style = EYE_STYLE_OPEN,   .width = 70, .height = 16, .pupil_dx = 4,  .pupil_dy = -1, .pupil_radius = 6, .brow_outer_y = 17, .brow_inner_y = 15},
        .right = {.style = EYE_STYLE_OPEN,   .width = 70, .height = 16, .pupil_dx = 2,  .pupil_dy = 2,  .pupil_radius = 6, .brow_outer_y = 16, .brow_inner_y = 17},
        .mouth_frames = MOUTH_THOUGHTFUL, .mouth_frame_count = 2, .mouth_frame_period = 18,
    },
    [EXP_SHY] = {
        .left =  {.style = EYE_STYLE_OPEN,   .width = 68, .height = 16, .pupil_dx = -2, .pupil_dy = 4,  .pupil_radius = 5, .brow_outer_y = 18, .brow_inner_y = 20},
        .right = {.style = EYE_STYLE_OPEN,   .width = 68, .height = 16, .pupil_dx = 2,  .pupil_dy = 4,  .pupil_radius = 5, .brow_outer_y = 18, .brow_inner_y = 20},
        .mouth_frames = MOUTH_SHY, .mouth_frame_count = 2, .mouth_frame_period = 16,
    },
    [EXP_FUNNY] = {
        .left =  {.style = EYE_STYLE_OPEN,   .width = 72, .height = 20, .pupil_dx = -1, .pupil_dy = 0,  .pupil_radius = 6, .brow_outer_y = 15, .brow_inner_y = 18},
        .right = {.style = EYE_STYLE_CLOSED, .width = 72, .height = 10, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 0, .brow_outer_y = 18, .brow_inner_y = 13},
        .mouth_frames = MOUTH_FUNNY, .mouth_frame_count = 2, .mouth_frame_period = 10,
    },
    [EXP_SURPRISED] = {
        .left =  {.style = EYE_STYLE_WIDE,   .width = 76, .height = 30, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 6, .brow_outer_y = 11, .brow_inner_y = 11},
        .right = {.style = EYE_STYLE_WIDE,   .width = 76, .height = 30, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 6, .brow_outer_y = 11, .brow_inner_y = 11},
        .mouth_frames = MOUTH_SURPRISED, .mouth_frame_count = 2, .mouth_frame_period = 8,
    },
    [EXP_EXCITED] = {
        .left =  {.style = EYE_STYLE_WIDE,   .width = 78, .height = 24, .pupil_dx = 0,  .pupil_dy = -2, .pupil_radius = 7, .brow_outer_y = 15, .brow_inner_y = 17},
        .right = {.style = EYE_STYLE_WIDE,   .width = 78, .height = 24, .pupil_dx = 0,  .pupil_dy = -2, .pupil_radius = 7, .brow_outer_y = 15, .brow_inner_y = 17},
        .mouth_frames = MOUTH_EXCITED, .mouth_frame_count = 2, .mouth_frame_period = 8,
    },
    [EXP_WINK] = {
        .left =  {.style = EYE_STYLE_OPEN,   .width = 74, .height = 21, .pupil_dx = -1, .pupil_dy = -1, .pupil_radius = 6, .brow_outer_y = 16, .brow_inner_y = 19},
        .right = {.style = EYE_STYLE_CLOSED, .width = 74, .height = 10, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 0, .brow_outer_y = 19, .brow_inner_y = 14},
        .mouth_frames = MOUTH_WINK, .mouth_frame_count = 1, .mouth_frame_period = 1,
    },
    [EXP_LOVE] = {
        .left =  {.style = EYE_STYLE_WIDE,   .width = 70, .height = 24, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 7, .brow_outer_y = 15, .brow_inner_y = 18},
        .right = {.style = EYE_STYLE_WIDE,   .width = 70, .height = 24, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 7, .brow_outer_y = 15, .brow_inner_y = 18},
        .mouth_frames = MOUTH_LOVE, .mouth_frame_count = 1, .mouth_frame_period = 1,
    },
    [EXP_SLEEP] = {
        .left =  {.style = EYE_STYLE_CLOSED, .width = 76, .height = 8,  .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 0, .brow_outer_y = 18, .brow_inner_y = 19},
        .right = {.style = EYE_STYLE_CLOSED, .width = 76, .height = 8,  .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 0, .brow_outer_y = 18, .brow_inner_y = 19},
        .mouth_frames = MOUTH_SLEEP, .mouth_frame_count = 2, .mouth_frame_period = 20,
    },
    [EXP_SCAN] = {
        .left =  {.style = EYE_STYLE_WIDE,   .width = 74, .height = 22, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 5, .brow_outer_y = 16, .brow_inner_y = 16},
        .right = {.style = EYE_STYLE_WIDE,   .width = 74, .height = 22, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 5, .brow_outer_y = 16, .brow_inner_y = 16},
        .mouth_frames = MOUTH_SCAN, .mouth_frame_count = 2, .mouth_frame_period = 6,
    },
    [EXP_LOST] = {
        .left =  {.style = EYE_STYLE_SQUINT, .width = 72, .height = 9,  .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 0, .brow_outer_y = 16, .brow_inner_y = 20},
        .right = {.style = EYE_STYLE_SQUINT, .width = 72, .height = 9,  .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 0, .brow_outer_y = 16, .brow_inner_y = 20},
        .mouth_frames = MOUTH_LOST, .mouth_frame_count = 1, .mouth_frame_period = 1,
    },
    [EXP_CUSTOM] = {
        .left =  {.style = EYE_STYLE_OPEN,   .width = 72, .height = 20, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 7, .brow_outer_y = 18, .brow_inner_y = 18},
        .right = {.style = EYE_STYLE_OPEN,   .width = 72, .height = 20, .pupil_dx = 0,  .pupil_dy = 0,  .pupil_radius = 7, .brow_outer_y = 18, .brow_inner_y = 18},
        .mouth_frames = MOUTH_NEUTRAL, .mouth_frame_count = 1, .mouth_frame_period = 1,
    },
};

static void init_i2c_ports(void) {
    i2c_config_t conf0 = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CURIE_LEFT_OLED_SDA_GPIO,
        .scl_io_num = CURIE_LEFT_OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CURIE_OLED_I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(CURIE_LEFT_OLED_I2C_PORT, &conf0));
    ESP_ERROR_CHECK(i2c_driver_install(CURIE_LEFT_OLED_I2C_PORT, conf0.mode, 0, 0, 0));

    i2c_config_t conf1 = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CURIE_RIGHT_OLED_SDA_GPIO,
        .scl_io_num = CURIE_RIGHT_OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CURIE_OLED_I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(CURIE_RIGHT_OLED_I2C_PORT, &conf1));
    ESP_ERROR_CHECK(i2c_driver_install(CURIE_RIGHT_OLED_I2C_PORT, conf1.mode, 0, 0, 0));
}

#define I2C_BUFFER_SIZE 1024

static uint8_t u8g2_esp32_i2c_byte_cb_left(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[I2C_BUFFER_SIZE];
    static uint16_t buf_idx = 0;

    switch (msg) {
        case U8X8_MSG_BYTE_SEND:
            if (buf_idx + arg_int <= I2C_BUFFER_SIZE) {
                memcpy(&buffer[buf_idx], arg_ptr, arg_int);
                buf_idx += arg_int;
            }
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            i2c_master_write_to_device(CURIE_LEFT_OLED_I2C_PORT, u8x8_GetI2CAddress(u8x8) >> 1, buffer, buf_idx, pdMS_TO_TICKS(100));
            break;
    }
    return 1;
}

static uint8_t u8g2_esp32_i2c_byte_cb_right(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[I2C_BUFFER_SIZE];
    static uint16_t buf_idx = 0;

    switch (msg) {
        case U8X8_MSG_BYTE_SEND:
            if (buf_idx + arg_int <= I2C_BUFFER_SIZE) {
                memcpy(&buffer[buf_idx], arg_ptr, arg_int);
                buf_idx += arg_int;
            }
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            i2c_master_write_to_device(CURIE_RIGHT_OLED_I2C_PORT, u8x8_GetI2CAddress(u8x8) >> 1, buffer, buf_idx, pdMS_TO_TICKS(100));
            break;
    }
    return 1;
}

static uint8_t u8g2_esp32_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    if (msg == U8X8_MSG_DELAY_MILLI) {
        vTaskDelay(pdMS_TO_TICKS(arg_int));
    }
    return 1;
}

static int triangle_wave(int phase, int max_value) {
    int wrapped = phase & 7;
    int mirrored = wrapped < 4 ? wrapped : 7 - wrapped;
    return (mirrored * max_value) / 3;
}

static void animate_eye_profile(robot_expression_t exp, bool is_left, const eye_profile_t *base, int phase, eye_profile_t *out) {
    *out = *base;

    switch (exp) {
        case EXP_HAPPY:
            out->pupil_dy -= phase == 2 ? 1 : 0;
            break;
        case EXP_FEAR:
            out->pupil_dx += is_left ? triangle_wave(phase + 1, 1) - 1 : 1 - triangle_wave(phase + 1, 1);
            out->pupil_dy += phase & 1;
            break;
        case EXP_CONFUSED:
            out->pupil_dx += is_left ? ((phase & 2) ? 1 : -1) : ((phase & 2) ? -1 : 1);
            break;
        case EXP_THOUGHTFUL:
            out->pupil_dx += is_left ? ((phase < 4) ? 2 : 4) : ((phase < 4) ? -1 : 1);
            out->pupil_dy -= phase < 4 ? 1 : 0;
            break;
        case EXP_SHY:
            out->pupil_dy += 1 + (phase & 1);
            break;
        case EXP_SURPRISED:
            out->pupil_dy += phase & 1;
            break;
        case EXP_EXCITED:
            out->pupil_dy -= phase & 1;
            break;
        case EXP_SCAN:
            out->pupil_dx = (phase - 4) * 2;
            break;
        case EXP_SLEEP:
            out->brow_inner_y += phase & 1;
            out->brow_outer_y += phase & 1;
            break;
        default:
            break;
    }
}

static void draw_brow(u8g2_t *u8g2, bool is_left, int outer_y, int inner_y) {
    const int outer_x = is_left ? EYE_OUTER_X : EYE_INNER_X;
    const int inner_x = is_left ? EYE_INNER_X : EYE_OUTER_X;
    for (int offset = 0; offset < BROW_THICK; offset++) {
        u8g2_DrawLine(u8g2, outer_x, outer_y + offset, inner_x, inner_y + offset);
    }
}

static void draw_eye_shape(u8g2_t *u8g2, const eye_profile_t *profile) {
    const int x = EYE_CENTER_X - (profile->width / 2);
    const int y = EYE_CENTER_Y - (profile->height / 2);

    switch (profile->style) {
        case EYE_STYLE_CLOSED:
            u8g2_DrawRBox(u8g2, EYE_BASE_X, EYE_CENTER_Y - 2, 80, 5, 2);
            return;

        case EYE_STYLE_SQUINT:
            u8g2_DrawRBox(u8g2, x, y, profile->width, profile->height, profile->height / 2);
            break;

        case EYE_STYLE_WIDE:
            u8g2_DrawRBox(u8g2, x, y, profile->width, profile->height, profile->height / 2);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawBox(u8g2, x - 1, y - 3, profile->width + 2, 3);
            u8g2_DrawBox(u8g2, x - 1, y + profile->height, profile->width + 2, 3);
            u8g2_SetDrawColor(u8g2, 1);
            break;

        case EYE_STYLE_OPEN:
        default:
            u8g2_DrawRBox(u8g2, x, y, profile->width, profile->height, profile->height / 2);
            break;
    }

    if (profile->pupil_radius == 0) {
        return;
    }

    const int pupil_x = EYE_CENTER_X + profile->pupil_dx;
    const int pupil_y = EYE_CENTER_Y + profile->pupil_dy;
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawDisc(u8g2, pupil_x, pupil_y, profile->pupil_radius, U8G2_DRAW_ALL);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawDisc(u8g2, pupil_x - 2, pupil_y - 2, 2, U8G2_DRAW_ALL);
}

static void draw_blink(u8g2_t *u8g2) {
    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawRBox(u8g2, 22, 28, 84, 7, 3);
    u8g2_SendBuffer(u8g2);
}

static void draw_eye(u8g2_t *u8g2, robot_expression_t exp, bool is_left, int phase) {
    if (exp < 0 || exp >= EXP_COUNT) {
        exp = EXP_HAPPY;
    }

    const face_profile_t *profile = &FACE_PROFILES[exp];
    eye_profile_t animated_eye;
    const eye_profile_t *base_eye = is_left ? &profile->left : &profile->right;

    animate_eye_profile(exp, is_left, base_eye, phase, &animated_eye);

    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);

    if (exp == EXP_LOVE) {
        u8g2_DrawDisc(u8g2, 52, 24, 14, U8G2_DRAW_ALL);
        u8g2_DrawDisc(u8g2, 76, 24, 14, U8G2_DRAW_ALL);
        u8g2_DrawTriangle(u8g2, 38, 28, 90, 28, 64, 54);
    } else if (exp == EXP_LOST) {
        u8g2_DrawLine(u8g2, 36, 18, 92, 44);
        u8g2_DrawLine(u8g2, 36, 44, 92, 18);
        u8g2_DrawLine(u8g2, 34, 18, 90, 44);
        u8g2_DrawLine(u8g2, 38, 44, 94, 18);
    } else {
        draw_eye_shape(u8g2, &animated_eye);
    }

    draw_brow(u8g2, is_left, animated_eye.brow_outer_y, animated_eye.brow_inner_y);
    u8g2_SendBuffer(u8g2);
}

static void draw_mouth_rows(const uint16_t rows[8]) {
    const int left_device = CURIE_MOUTH_SWAP_HALVES ? 1 : 0;
    const int right_device = CURIE_MOUTH_SWAP_HALVES ? 0 : 1;

    for (int row = 0; row < 8; row++) {
        int src_row_left = CURIE_MOUTH_LEFT_FLIP_ROWS ? (7 - row) : row;
        int src_row_right = CURIE_MOUTH_RIGHT_FLIP_ROWS ? (7 - row) : row;
        uint8_t left_bits = (rows[src_row_left] >> 8) & 0xFF;
        uint8_t right_bits = rows[src_row_right] & 0xFF;

        if (CURIE_MOUTH_LEFT_FLIP_COLS) {
            left_bits = reverse_bits8(left_bits);
        }
        if (CURIE_MOUTH_RIGHT_FLIP_COLS) {
            right_bits = reverse_bits8(right_bits);
        }

        max7219_set_row(left_device, row, left_bits);
        max7219_set_row(right_device, row, right_bits);
    }
}

static void draw_mouth(robot_expression_t exp, int phase) {
    if (exp == EXP_CUSTOM) {
        uint8_t custom[16];
        robot_get_custom_mouth(custom);
        const int left_device = CURIE_MOUTH_SWAP_HALVES ? 1 : 0;
        const int right_device = CURIE_MOUTH_SWAP_HALVES ? 0 : 1;
        for (int row = 0; row < 8; row++) {
            int src_row_left = CURIE_MOUTH_LEFT_FLIP_ROWS ? (7 - row) : row;
            int src_row_right = CURIE_MOUTH_RIGHT_FLIP_ROWS ? (7 - row) : row;
            uint8_t left_bits = custom[src_row_left];
            uint8_t right_bits = custom[8 + src_row_right];

            if (CURIE_MOUTH_LEFT_FLIP_COLS) {
                left_bits = reverse_bits8(left_bits);
            }
            if (CURIE_MOUTH_RIGHT_FLIP_COLS) {
                right_bits = reverse_bits8(right_bits);
            }

            max7219_set_row(left_device, row, left_bits);
            max7219_set_row(right_device, row, right_bits);
        }
        return;
    }

    if (exp < 0 || exp >= EXP_COUNT) {
        exp = EXP_HAPPY;
    }

    const face_profile_t *profile = &FACE_PROFILES[exp];
    int frame_index = 0;
    if (profile->mouth_frame_count > 1) {
        int period = profile->mouth_frame_period > 0 ? profile->mouth_frame_period : 1;
        frame_index = (phase / period) % profile->mouth_frame_count;
    }

    draw_mouth_rows(profile->mouth_frames[frame_index]);
}

void task_display(void *arg) {
    ESP_LOGI(TAG, "Initializing Displays...");
    init_i2c_ports();
    max7219_init();

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_left, U8G2_R0, u8g2_esp32_i2c_byte_cb_left, u8g2_esp32_gpio_and_delay_cb);
    u8g2_SetI2CAddress(&u8g2_left, CURIE_LEFT_OLED_I2C_ADDR);
    u8g2_InitDisplay(&u8g2_left);
    u8g2_SetPowerSave(&u8g2_left, 0);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_right, U8G2_R0, u8g2_esp32_i2c_byte_cb_right, u8g2_esp32_gpio_and_delay_cb);
    u8g2_SetI2CAddress(&u8g2_right, CURIE_RIGHT_OLED_I2C_ADDR);
    u8g2_InitDisplay(&u8g2_right);
    u8g2_SetPowerSave(&u8g2_right, 0);

    robot_expression_t last_exp = -1;
    int last_bright = -1;
    bool was_on = true;

    int blink_counter = 0;
    int next_blink = BLINK_MIN_TICKS + (esp_random() % (BLINK_MAX_TICKS - BLINK_MIN_TICKS));

    while (1) {
        bool is_on = robot_get_power();
        robot_state_t state = robot_state_get();

        if (!is_on) {
            if (was_on) {
                u8g2_ClearBuffer(&u8g2_left);
                u8g2_SendBuffer(&u8g2_left);
                u8g2_ClearBuffer(&u8g2_right);
                u8g2_SendBuffer(&u8g2_right);
                max7219_clear();
                max7219_shutdown(true);
                was_on = false;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        } else if (!was_on) {
            max7219_shutdown(false);
            last_exp = -1;
            last_bright = -1;
            was_on = true;
        }

        robot_expression_t cur_exp = robot_get_expression();
        int cur_bright = robot_get_brightness();

        if (state == ROBOT_STATE_ERROR) {
            cur_exp = EXP_SAD;
        } else if (state == ROBOT_STATE_LOW_POWER) {
            cur_bright = 1;
        }

        bool force_blink = robot_get_and_clear_blink();
        blink_counter++;

        if (state == ROBOT_STATE_IDLE && next_blink > (BLINK_MIN_TICKS * 3) / 2) {
            next_blink = BLINK_MIN_TICKS;
        }

        if (blink_counter >= next_blink || force_blink) {
            draw_blink(&u8g2_left);
            draw_blink(&u8g2_right);
            vTaskDelay(pdMS_TO_TICKS(BLINK_DURATION_MS));
            last_exp = -1;
            blink_counter = 0;
            next_blink = BLINK_MIN_TICKS + (esp_random() % (BLINK_MAX_TICKS - BLINK_MIN_TICKS));
        }

        int phase = (int)((xTaskGetTickCount() / pdMS_TO_TICKS(120)) & 7);
        if (cur_exp != last_exp || cur_exp == EXP_SCAN || cur_exp == EXP_EXCITED || cur_exp == EXP_SURPRISED ||
            cur_exp == EXP_CONFUSED || cur_exp == EXP_THOUGHTFUL || cur_exp == EXP_SHY ||
            cur_exp == EXP_FEAR || cur_exp == EXP_FUNNY || cur_exp == EXP_SLEEP) {
            draw_eye(&u8g2_left, cur_exp, true, phase);
            draw_eye(&u8g2_right, cur_exp, false, phase);
            draw_mouth(cur_exp, phase);
            last_exp = cur_exp;
        }

        if (cur_bright != last_bright) {
            max7219_set_intensity(cur_bright);
            last_bright = cur_bright;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

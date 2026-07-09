#include "espnow_controller.h"
#include "events/event_bus.h"
#include "state/robot_state.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ESPNOW_CTRL";

#define RX_QUEUE_DEPTH 8
#define FAILSAFE_TIMEOUT_US 500000
#define ARM_STEP_DEG 2
#define ARM_HOME_DEG 90
#define ARM_MIN_DEG 0
#define ARM_MAX_DEG 180

typedef struct {
    uint8_t mac[6];
    uint8_t data[CURIE_ESPNOW_PACKET_SIZE];
    int len;
} espnow_rx_msg_t;

static QueueHandle_t s_rx_queue = NULL;
static bool s_controller_seen = false;
static bool s_failsafe_active = false;
static int64_t s_last_packet_us = 0;
static robot_expression_t s_expression_before_failsafe = EXP_HAPPY;
static bool s_arm_target_valid = false;
static int s_left_shoulder_target = ARM_HOME_DEG;
static bool s_estop_sent = false;

static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static uint8_t checksum_xor(const uint8_t *data) {
    uint8_t out = 0;
    for (int i = 0; i < CURIE_ESPNOW_PACKET_SIZE - 1; i++) {
        out ^= data[i];
    }
    return out;
}

static int axis_to_signed(uint8_t value) {
    int centered = (int)value - 128;
    if (centered >= 0) return (centered * 100) / 127;
    return (centered * 100) / 128;
}

static robot_expression_t expression_from_id(uint8_t id) {
    static const robot_expression_t expressions[13] = {
        EXP_HAPPY,
        EXP_HAPPY,
        EXP_NEUTRAL,
        EXP_SAD,
        EXP_WINK,
        EXP_LOVE,
        EXP_ANGRY,
        EXP_SLEEP,
        EXP_SCAN,
        EXP_SURPRISE,
        EXP_CURIOUS,
        EXP_EXCITED,
        EXP_CONFUSED,
    };

    if (id > 12) return EXP_HAPPY;
    return expressions[id];
}

static void send_drive(robot_drive_t drive) {
    event_payload_t payload = {0};
    payload.drive = drive;
    event_bus_send(EVT_SET_DRIVE, EVT_SRC_ESPNOW, payload);
}

static void send_expression(robot_expression_t expression) {
    event_payload_t payload = {0};
    payload.expression = expression;
    event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_ESPNOW, payload);
}

static void send_mirrored_shoulders(int left_angle) {
    left_angle = clamp_int(left_angle, ARM_MIN_DEG, ARM_MAX_DEG);
    s_left_shoulder_target = left_angle;
    s_arm_target_valid = true;

    event_payload_t payload = {0};
    payload.arms = (robot_arms_t){
        .ls = left_angle,
        .le = 45,
        .rs = 180 - left_angle,
        .re = 45,
    };
    event_bus_send(EVT_SET_ARMS, EVT_SRC_ESPNOW, payload);
}

static void ensure_arm_target(void) {
    if (s_arm_target_valid) return;
    robot_arms_t arms = robot_get_arms();
    s_left_shoulder_target = clamp_int(arms.ls, ARM_MIN_DEG, ARM_MAX_DEG);
    s_arm_target_valid = true;
}

static robot_drive_t mix_drive(uint8_t throttle_raw, uint8_t steering_raw, uint8_t speed_mode) {
    static const int speed_caps[] = {40, 70, 100};
    int cap = speed_caps[clamp_int(speed_mode, 0, 2)];

    int throttle = -axis_to_signed(throttle_raw);
    int steering = axis_to_signed(steering_raw);

    int left = clamp_int(throttle + steering, -100, 100);
    int right = clamp_int(throttle - steering, -100, 100);

    left = (left * cap) / 100;
    right = (right * cap) / 100;
    return (robot_drive_t){left, right};
}

static void process_packet(const uint8_t *data) {
    if (data[0] != CURIE_ESPNOW_MAGIC) return;
    if (checksum_xor(data) != data[7]) return;

    const uint8_t throttle = data[1];
    const uint8_t steering = data[2];
    const uint8_t buttons = data[3];
    const uint8_t expression_id = data[4];
    const uint8_t speed_mode = data[5];

    s_last_packet_us = esp_timer_get_time();
    s_controller_seen = true;

    if (s_failsafe_active) {
        s_failsafe_active = false;
        if (s_expression_before_failsafe != EXP_LOST) {
            send_expression(s_expression_before_failsafe);
        }
    }

    if (buttons & CURIE_CTRL_BTN_CLEAR_ESTOP) {
        event_bus_send_simple(EVT_CLEAR_ERROR, EVT_SRC_ESPNOW);
        s_estop_sent = false;
    }

    if (buttons & CURIE_CTRL_BTN_ESTOP) {
        send_drive((robot_drive_t){0, 0});
        if (!s_estop_sent) {
            event_bus_send_simple(EVT_ESTOP, EVT_SRC_ESPNOW);
            s_estop_sent = true;
        }
        return;
    }

    s_estop_sent = false;

    event_payload_t speed_payload = {0};
    speed_payload.int_val = clamp_int(speed_mode, 0, 2) == 0 ? 40 : (clamp_int(speed_mode, 0, 2) == 1 ? 70 : 100);
    event_bus_send(EVT_SET_SPEED, EVT_SRC_ESPNOW, speed_payload);

    if (buttons & CURIE_CTRL_BTN_HOME) {
        send_drive((robot_drive_t){0, 0});
        send_mirrored_shoulders(ARM_HOME_DEG);
        send_expression(EXP_NEUTRAL);
        return;
    }

    send_drive(mix_drive(throttle, steering, speed_mode));

    if (expression_id > 0 && expression_id <= 12) {
        send_expression(expression_from_id(expression_id));
    }

    if ((buttons & CURIE_CTRL_BTN_ARM_UP) || (buttons & CURIE_CTRL_BTN_ARM_DOWN)) {
        ensure_arm_target();
        if (buttons & CURIE_CTRL_BTN_ARM_UP) {
            s_left_shoulder_target += ARM_STEP_DEG;
        }
        if (buttons & CURIE_CTRL_BTN_ARM_DOWN) {
            s_left_shoulder_target -= ARM_STEP_DEG;
        }
        send_mirrored_shoulders(s_left_shoulder_target);
    }
}

static void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!s_rx_queue || len != CURIE_ESPNOW_PACKET_SIZE) return;

    espnow_rx_msg_t msg = {0};
    if (recv_info && recv_info->src_addr) {
        memcpy(msg.mac, recv_info->src_addr, 6);
    }
    memcpy(msg.data, data, CURIE_ESPNOW_PACKET_SIZE);
    msg.len = len;
    xQueueSend(s_rx_queue, &msg, 0);
}

static void task_espnow_controller(void *arg) {
    espnow_rx_msg_t msg;

    while (1) {
        while (xQueueReceive(s_rx_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            process_packet(msg.data);
        }

        if (s_controller_seen && !s_failsafe_active) {
            int64_t now_us = esp_timer_get_time();
            if (now_us - s_last_packet_us > FAILSAFE_TIMEOUT_US) {
                s_expression_before_failsafe = robot_get_expression();
                send_drive((robot_drive_t){0, 0});
                send_expression(EXP_LOST);
                s_failsafe_active = true;
                ESP_LOGW(TAG, "Controller failsafe triggered");
            }
        }
    }
}

void start_espnow_controller(void) {
    if (s_rx_queue) return;

    s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(espnow_rx_msg_t));
    configASSERT(s_rx_queue);

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    xTaskCreatePinnedToCore(task_espnow_controller, "task_espnow_controller", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "ESP-NOW controller receiver started");
}

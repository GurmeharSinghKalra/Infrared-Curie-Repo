#include "ble_controller.h"
#include "events/event_bus.h"
#include "state/robot_state.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE_CTRL";

#define RX_QUEUE_DEPTH 8
#define FAILSAFE_TIMEOUT_US 500000
#define LINK_WATCHDOG_TIMEOUT_US 10000000
#define DIAGNOSTIC_LOG_INTERVAL_US 10000000
#define DRIVE_LOG_INTERVAL_US 250000
#define ARM_STEP_DEG 2
#define ARM_HOME_DEG 90
#define ARM_MIN_DEG 0
#define ARM_MAX_DEG 180
#define CURIE_MAX_EXPRESSION_ID 17

#define CURIE_CTRL_BTN_ESTOP (1 << 0)
#define CURIE_CTRL_BTN_HOME (1 << 1)
#define CURIE_CTRL_BTN_ARM_UP (1 << 2)
#define CURIE_CTRL_BTN_ARM_DOWN (1 << 3)
#define CURIE_CTRL_BTN_CLEAR_ESTOP (1 << 4)
#define CURIE_CTRL_BTN_BLINK (1 << 5)

static const uint8_t CURIE_BLE_MAGIC = 0xC4;

typedef struct {
    uint8_t data[CURIE_BLE_PACKET_SIZE];
    int len;
} ble_rx_msg_t;

static QueueHandle_t s_rx_queue = NULL;
static bool s_controller_seen = false;
static bool s_failsafe_active = false;
static int64_t s_last_packet_us = 0;
static robot_expression_t s_expression_before_failsafe = EXP_HAPPY;
static bool s_arm_target_valid = false;
static int s_left_shoulder_target = ARM_HOME_DEG;
static bool s_estop_sent = false;
static uint8_t own_addr_type;
static bool s_gap_connected = false;
static bool s_adv_active = false;
static uint16_t s_conn_handle = 0;
static int64_t s_connected_since_us = 0;
static int64_t s_last_diag_log_us = 0;
static int64_t s_last_drive_log_us = 0;
static robot_drive_t s_last_logged_drive = {0};
static int s_last_logged_speed_pct = -1;
static uint32_t s_valid_packet_count = 0;
static uint32_t s_invalid_magic_count = 0;
static uint32_t s_invalid_checksum_count = 0;
static uint32_t s_queue_drop_count = 0;

static int ble_gap_event(struct ble_gap_event *event, void *arg);

static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void log_drive_if_needed(robot_drive_t drive) {
    int64_t now_us = esp_timer_get_time();
    bool changed = drive.left != s_last_logged_drive.left || drive.right != s_last_logged_drive.right;
    bool interval_elapsed = (now_us - s_last_drive_log_us) >= DRIVE_LOG_INTERVAL_US;
    bool important = (drive.left == 0 && drive.right == 0) ||
                     (s_last_logged_drive.left == 0 && s_last_logged_drive.right == 0);

    if (changed && (important || interval_elapsed)) {
        ESP_LOGI(TAG, "Action drive: left=%d right=%d", drive.left, drive.right);
        s_last_logged_drive = drive;
        s_last_drive_log_us = now_us;
    }
}

static void log_ble_status(void) {
    int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_diag_log_us) < DIAGNOSTIC_LOG_INTERVAL_US) {
        return;
    }
    s_last_diag_log_us = now_us;

    ESP_LOGI(TAG,
             "BLE status: gap_connected=%d adv_active=%d conn=%u packets=%lu bad_magic=%lu bad_crc=%lu queue_drop=%lu last_pkt_ms=%lld failsafe=%d",
             s_gap_connected,
             s_adv_active,
             (unsigned)s_conn_handle,
             (unsigned long)s_valid_packet_count,
             (unsigned long)s_invalid_magic_count,
             (unsigned long)s_invalid_checksum_count,
             (unsigned long)s_queue_drop_count,
             s_last_packet_us > 0 ? (long long)((now_us - s_last_packet_us) / 1000) : -1LL,
             s_failsafe_active);
}

static uint8_t checksum_xor(const uint8_t *data) {
    uint8_t out = 0;
    for (int i = 0; i < CURIE_BLE_PACKET_SIZE - 1; i++) {
        out ^= data[i];
    }
    return out;
}

static int axis_to_signed(uint8_t value) {
    int centered = (int)value - 128;
    if (centered >= 0) return (centered * 100) / 127;
    return (centered * 100) / 128;
}

static const char *expression_name(robot_expression_t expression) {
    switch (expression) {
        case EXP_HAPPY: return "happy";
        case EXP_SAD: return "sad";
        case EXP_ANGRY: return "angry";
        case EXP_FEAR: return "fear";
        case EXP_DISGUST: return "disgust";
        case EXP_CONFUSED: return "confused";
        case EXP_CONTEMPT: return "contempt";
        case EXP_THOUGHTFUL: return "thoughtful";
        case EXP_SHY: return "shy";
        case EXP_FUNNY: return "funny";
        case EXP_SURPRISED: return "surprised";
        case EXP_EXCITED: return "excited";
        case EXP_NEUTRAL: return "neutral";
        case EXP_WINK: return "wink";
        case EXP_LOVE: return "love";
        case EXP_SLEEP: return "sleep";
        case EXP_SCAN: return "scan";
        case EXP_LOST: return "lost";
        default: return "unknown";
    }
}

static robot_expression_t expression_from_id(uint8_t id) {
    static const robot_expression_t expressions[CURIE_MAX_EXPRESSION_ID] = {
        EXP_HAPPY, EXP_SAD, EXP_ANGRY, EXP_FEAR, EXP_DISGUST,
        EXP_CONFUSED, EXP_CONTEMPT, EXP_THOUGHTFUL, EXP_SHY,
        EXP_FUNNY, EXP_SURPRISED, EXP_EXCITED, EXP_NEUTRAL,
        EXP_WINK, EXP_LOVE, EXP_SLEEP, EXP_SCAN,
    };
    if (id == 0 || id > CURIE_MAX_EXPRESSION_ID) return EXP_HAPPY;
    return expressions[id - 1];
}

static void send_drive(robot_drive_t drive) {
    event_payload_t payload = {0};
    payload.drive = drive;
    event_bus_send(EVT_SET_DRIVE, EVT_SRC_ESPNOW, payload);
    log_drive_if_needed(drive);
}

static void send_expression(robot_expression_t expression) {
    event_payload_t payload = {0};
    payload.expression = expression;
    event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_ESPNOW, payload);
    ESP_LOGI(TAG, "Action expression: %s", expression_name(expression));
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
    ESP_LOGI(TAG, "Action arms: ls=%d rs=%d", payload.arms.ls, payload.arms.rs);
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

static void stop_and_mark_link_lost(const char *reason) {
    if (!s_failsafe_active) {
        s_expression_before_failsafe = robot_get_expression();
        send_drive((robot_drive_t){0, 0});
        send_expression(EXP_LOST);
        s_failsafe_active = true;
    }
    ESP_LOGW(TAG, "BLE link lost: %s", reason);
}

static void process_packet(const uint8_t *data) {
    if (data[0] != CURIE_BLE_MAGIC) {
        s_invalid_magic_count++;
        return;
    }
    if (checksum_xor(data) != data[7]) {
        s_invalid_checksum_count++;
        return;
    }

    const uint8_t throttle = data[1];
    const uint8_t steering = data[2];
    const uint8_t buttons = data[3];
    const uint8_t expression_id = data[4];
    const uint8_t speed_mode = data[5];

    s_last_packet_us = esp_timer_get_time();
    s_controller_seen = true;
    s_valid_packet_count++;

    if (s_failsafe_active) {
        s_failsafe_active = false;
        ESP_LOGI(TAG, "BLE link restored");
        if (s_expression_before_failsafe != EXP_LOST) {
            send_expression(s_expression_before_failsafe);
        }
    }

    if (buttons & CURIE_CTRL_BTN_CLEAR_ESTOP) {
        event_bus_send_simple(EVT_CLEAR_ERROR, EVT_SRC_ESPNOW);
        s_estop_sent = false;
        ESP_LOGI(TAG, "Action clear_estop");
    }

    if (buttons & CURIE_CTRL_BTN_ESTOP) {
        send_drive((robot_drive_t){0, 0});
        if (!s_estop_sent) {
            event_bus_send_simple(EVT_ESTOP, EVT_SRC_ESPNOW);
            s_estop_sent = true;
            ESP_LOGW(TAG, "Action estop");
        }
        return;
    }

    s_estop_sent = false;

    event_payload_t speed_payload = {0};
    speed_payload.int_val = clamp_int(speed_mode, 0, 2) == 0 ? 40 : (clamp_int(speed_mode, 0, 2) == 1 ? 70 : 100);
    event_bus_send(EVT_SET_SPEED, EVT_SRC_ESPNOW, speed_payload);
    if (speed_payload.int_val != s_last_logged_speed_pct) {
        s_last_logged_speed_pct = speed_payload.int_val;
        ESP_LOGI(TAG, "Action speed: %d", speed_payload.int_val);
    }

    if (buttons & CURIE_CTRL_BTN_HOME) {
        send_drive((robot_drive_t){0, 0});
        send_mirrored_shoulders(ARM_HOME_DEG);
        send_expression(EXP_NEUTRAL);
        ESP_LOGI(TAG, "Action home");
        return;
    }

    send_drive(mix_drive(throttle, steering, speed_mode));

    if (expression_id > 0 && expression_id <= CURIE_MAX_EXPRESSION_ID) {
        send_expression(expression_from_id(expression_id));
    }

    if (buttons & CURIE_CTRL_BTN_BLINK) {
        event_bus_send_simple(EVT_BLINK, EVT_SRC_ESPNOW);
        ESP_LOGI(TAG, "Action blink");
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

static int ble_gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        int len = OS_MBUF_PKTLEN(ctxt->om);
        if (len == CURIE_BLE_PACKET_SIZE && s_rx_queue) {
            ble_rx_msg_t msg = {0};
            msg.len = len;
            os_mbuf_copydata(ctxt->om, 0, len, msg.data);
            if (xQueueSend(s_rx_queue, &msg, 0) != pdTRUE) {
                s_queue_drop_count++;
            }
        }
    }
    return 0;
}

static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_uuid.u,
                .access_cb = ble_gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 }
        },
    },
    { 0 }
};

static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = "Curie-Robot";
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        s_adv_active = false;
        ESP_LOGE(TAG, "Error starting advertisement: %d", rc);
    } else {
        s_adv_active = true;
        ESP_LOGI(TAG, "BLE advertising started");
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_gap_connected = true;
                s_adv_active = false;
                s_conn_handle = event->connect.conn_handle;
                s_connected_since_us = esp_timer_get_time();
                s_last_packet_us = s_connected_since_us;
                ESP_LOGI(TAG, "BLE controller connected: conn=%u", (unsigned)s_conn_handle);
            } else {
                ESP_LOGW(TAG, "BLE connect failed: %d", event->connect.status);
                s_gap_connected = false;
                s_adv_active = false;
                s_conn_handle = 0;
                ble_app_advertise();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE controller disconnected: reason=%d", event->disconnect.reason);
            s_gap_connected = false;
            s_adv_active = false;
            s_conn_handle = 0;
            stop_and_mark_link_lost("disconnect event");
            ble_app_advertise();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            s_adv_active = false;
            ble_app_advertise();
            return 0;

        default:
            return 0;
    }
}

static void ble_app_on_sync(void) {
    ble_hs_id_infer_auto(0, &own_addr_type);
    ble_app_advertise();
}

static void task_ble_processor(void *arg) {
    ble_rx_msg_t msg;

    while (1) {
        while (xQueueReceive(s_rx_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            process_packet(msg.data);
        }

        if (s_controller_seen && !s_failsafe_active) {
            int64_t now_us = esp_timer_get_time();
            if (now_us - s_last_packet_us > FAILSAFE_TIMEOUT_US) {
                stop_and_mark_link_lost("packet timeout");
            }
        }

        if (s_gap_connected) {
            int64_t now_us = esp_timer_get_time();
            if ((now_us - s_last_packet_us) > LINK_WATCHDOG_TIMEOUT_US) {
                ESP_LOGW(TAG, "BLE watchdog terminating stale link conn=%u", (unsigned)s_conn_handle);
                if (s_conn_handle != 0) {
                    int rc = ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
                        ESP_LOGW(TAG, "ble_gap_terminate rc=%d", rc);
                    }
                }
            }
        } else if (!s_adv_active) {
            ESP_LOGW(TAG, "BLE watchdog restarting advertising");
            ble_app_advertise();
        }

        log_ble_status();
    }
}

static void nimble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}



void start_ble_controller(void) {
    if (s_rx_queue) return;

    s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(ble_rx_msg_t));
    configASSERT(s_rx_queue);

    xTaskCreatePinnedToCore(task_ble_processor, "task_ble_processor", 4096, NULL, 6, NULL, 0);

    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE controller receiver started");
}

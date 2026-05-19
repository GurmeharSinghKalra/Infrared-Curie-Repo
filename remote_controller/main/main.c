#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "keypad.h"
#include "oled_ui.h"

// NimBLE Host headers
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "CTRL_BLE_CLIENT";

// Bitmask mappings for buttons
#define CURIE_CTRL_BTN_ESTOP (1 << 0)
#define CURIE_CTRL_BTN_HOME (1 << 1)
#define CURIE_CTRL_BTN_ARM_UP (1 << 2)
#define CURIE_CTRL_BTN_ARM_DOWN (1 << 3)
#define CURIE_CTRL_BTN_CLEAR_ESTOP (1 << 4)
#define CURIE_CTRL_BTN_BLINK (1 << 5)

static const uint8_t CURIE_BLE_MAGIC = 0xC4;
#define SEND_INTERVAL_MS 40
#define STATUS_LOG_INTERVAL_US 10000000
#define DISCOVERY_TIMEOUT_US 12000000
#define WRITE_ERROR_RECONNECT_THRESHOLD 4
#define JOYSTICK_CALIBRATION_SAMPLES 24
#define JOYSTICK_DEADZONE 250
#define CONTROLLER_BATTERY_ADC_ENABLED 0
#define CONTROLLER_BATTERY_DIVIDER_X1000 2000

// UUID Definitions
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

// Connection and discovery states
static uint16_t s_conn_handle = 0;
static uint16_t s_chr_value_handle = 0;
static bool s_connected = false;
static uint8_t own_addr_type;
static bool s_service_found = false;
static bool s_char_found = false;
static bool s_connecting = false;
static int64_t s_connect_started_us = 0;
static bool s_recovery_inflight = false;
static int64_t s_last_good_tx_us = 0;
static int64_t s_last_status_log_us = 0;
static uint32_t s_packets_sent = 0;
static uint32_t s_packets_failed = 0;
static uint32_t s_consecutive_write_errors = 0;

// Controller state variables
static uint8_t s_sequence = 0;
static uint8_t s_speed_mode = 1;
static uint8_t s_pending_expression = 0;
static uint8_t s_pending_button_latch = 0;
static uint8_t s_expression_page = 0;
static uint8_t s_current_expression_id = 13;

static bool s_joy_button_down = false;
static int64_t s_joy_button_press_us = 0;
static bool s_joy_long_handled = false;
static bool s_joy_combo_used = false;
static uint8_t s_last_logged_dpad_mask = 0xFF;
static int s_joy_center_x = 2048;
static int s_joy_center_y = 2048;

// ADC handle
static adc_oneshot_unit_handle_t s_adc_unit = NULL;

static void ble_client_scan(void);
static void buttons_init(void);
static void ble_client_force_rescan(const char *reason);
static void log_controller_status(void);
static const char *expression_label(uint8_t id);
static const char *speed_label(uint8_t mode);

static uint8_t checksum_xor(const uint8_t *data) {
    uint8_t out = 0;
    for (int i = 0; i < 7; i++) {
        out ^= data[i];
    }
    return out;
}

static uint8_t clamp_to_byte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static uint8_t analog_to_axis_byte(int raw, int center, bool invert) {
    if (abs(raw - center) <= JOYSTICK_DEADZONE) {
        return 128;
    }
    int adjusted = raw - center + 2048;
    adjusted = adjusted < 0 ? 0 : (adjusted > 4095 ? 4095 : adjusted);
    adjusted = (adjusted * 255) / 4095;
    if (invert) {
        adjusted = 255 - adjusted;
    }
    return clamp_to_byte(adjusted);
}

static const char *expression_label(uint8_t id) {
    switch (id) {
        case 1: return "HAPPY";
        case 2: return "SAD";
        case 3: return "MAD";
        case 4: return "FEAR";
        case 5: return "YUCK";
        case 6: return "HUH?";
        case 7: return "SMIRK";
        case 8: return "THINK";
        case 9: return "SHY";
        case 10: return "FUNNY";
        case 11: return "WOW";
        case 12: return "YAY";
        case 13: return "READY";
        case 14: return "WINK";
        case 15: return "LOVE";
        case 16: return "SLEEP";
        case 17: return "SCAN";
        default: return "READY";
    }
}

static const char *speed_label(uint8_t mode) {
    switch (mode) {
        case 0: return "Turtle";
        case 1: return "Cruise";
        case 2: return "Zoom";
        default: return "Cruise";
    }
}

static void clear_link_state(void) {
    s_connected = false;
    s_connecting = false;
    s_service_found = false;
    s_char_found = false;
    s_chr_value_handle = 0;
    s_conn_handle = 0;
    s_connect_started_us = 0;
    s_consecutive_write_errors = 0;
}

static void ble_client_force_rescan(const char *reason) {
    if (s_recovery_inflight) {
        ESP_LOGW(TAG, "Recovery already in flight, skipping duplicate rescan: %s", reason);
        return;
    }

    s_recovery_inflight = true;
    ESP_LOGW(TAG, "Forcing BLE recovery: %s", reason);
    if (s_conn_handle != 0) {
        int rc = ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN) {
            ESP_LOGW(TAG, "ble_gap_terminate rc=%d during recovery", rc);
        }
        if (rc == 0) {
            return;
        }
    }
    clear_link_state();
    s_recovery_inflight = false;
    ble_client_scan();
}

static void log_controller_status(void) {
    int64_t now = esp_timer_get_time();
    if ((now - s_last_status_log_us) < STATUS_LOG_INTERVAL_US) {
        return;
    }
    s_last_status_log_us = now;
    ESP_LOGI(TAG,
             "BLE status: connected=%d connecting=%d svc=%d chr=%d conn=%u handle=%u tx_ok=%lu tx_err=%lu last_tx_ms=%lld",
             s_connected,
             s_connecting,
             s_service_found,
             s_char_found,
             (unsigned)s_conn_handle,
             (unsigned)s_chr_value_handle,
             (unsigned long)s_packets_sent,
             (unsigned long)s_packets_failed,
             s_last_good_tx_us > 0 ? (long long)((now - s_last_good_tx_us) / 1000) : -1LL);
}

static uint8_t read_dpad_mask(void) {
    uint8_t mask = 0;
    if (gpio_get_level(GPIO_NUM_26) == 0) mask |= (1 << 0); // Up
    if (gpio_get_level(GPIO_NUM_27) == 0) mask |= (1 << 1); // Right
    if (gpio_get_level(GPIO_NUM_21) == 0) mask |= (1 << 2); // Down
    if (gpio_get_level(GPIO_NUM_22) == 0) mask |= (1 << 3); // Left
    return mask;
}

static void log_dpad_state(void) {
    uint8_t dpad = read_dpad_mask();
    bool up = (dpad & (1 << 0)) != 0;
    bool right = (dpad & (1 << 1)) != 0;
    bool down = (dpad & (1 << 2)) != 0;
    bool left = (dpad & (1 << 3)) != 0;

    if (dpad != s_last_logged_dpad_mask) {
        ESP_LOGI(TAG, "D-pad state: U=%d R=%d D=%d L=%d", up, right, down, left);
        s_last_logged_dpad_mask = dpad;
    }
}

static void calibrate_joystick(void) {
    int sum_x = 0;
    int sum_y = 0;

    for (int i = 0; i < JOYSTICK_CALIBRATION_SAMPLES; i++) {
        int raw_x = 2048;
        int raw_y = 2048;
        adc_oneshot_read(s_adc_unit, ADC_CHANNEL_4, &raw_x);
        adc_oneshot_read(s_adc_unit, ADC_CHANNEL_5, &raw_y);
        sum_x += raw_x;
        sum_y += raw_y;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_joy_center_x = sum_x / JOYSTICK_CALIBRATION_SAMPLES;
    s_joy_center_y = sum_y / JOYSTICK_CALIBRATION_SAMPLES;
    if (abs(s_joy_center_x - 2048) > 700) {
        s_joy_center_x = 2048;
    }
    if (abs(s_joy_center_y - 2048) > 700) {
        s_joy_center_y = 2048;
    }
    ESP_LOGI(TAG, "Joystick calibrated: center_x=%d center_y=%d", s_joy_center_x, s_joy_center_y);
}

// Characteristic Discovery Callback
static int ble_client_on_disc_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr, void *arg) {
    if (error->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &gatt_svr_chr_uuid.u) == 0) {
            s_char_found = true;
            s_chr_value_handle = chr->val_handle;
            s_conn_handle = conn_handle;
            s_connected = true;
            s_connecting = false;
            s_consecutive_write_errors = 0;
            s_last_good_tx_us = esp_timer_get_time();
            ESP_LOGI(TAG, "BLE Curie service and characteristic discovered! Value Handle: %d", chr->val_handle);
        }
    } else if (error->status == BLE_HS_EDONE) {
        if (!s_char_found) {
            ble_client_force_rescan("required characteristic not found");
        } else {
            s_connect_started_us = 0;
            ESP_LOGI(TAG, "BLE Characteristic discovery complete");
        }
    } else {
        if (error->status == BLE_HS_ENOTCONN) {
            ESP_LOGW(TAG, "Characteristic discovery ended after disconnect");
            return 0;
        }
        ESP_LOGE(TAG, "BLE Characteristic discovery error: %d", error->status);
        ble_client_force_rescan("characteristic discovery error");
    }
    return 0;
}

// Service Discovery Callback
static int ble_client_on_disc_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                  const struct ble_gatt_svc *svc, void *arg) {
    if (error->status == 0) {
        if (ble_uuid_cmp(&svc->uuid.u, &gatt_svr_svc_uuid.u) == 0) {
            s_service_found = true;
            ESP_LOGI(TAG, "Discovered Curie Service! Finding characteristics...");
            ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle,
                                    ble_client_on_disc_chr, NULL);
        }
    } else if (error->status == BLE_HS_EDONE) {
        if (!s_service_found) {
            ble_client_force_rescan("required service not found");
        } else {
            ESP_LOGI(TAG, "BLE Service discovery complete");
        }
    } else {
        if (error->status == BLE_HS_ENOTCONN) {
            ESP_LOGW(TAG, "Service discovery ended after disconnect");
            return 0;
        }
        ESP_LOGE(TAG, "BLE Service discovery error: %d", error->status);
        ble_client_force_rescan("service discovery error");
    }
    return 0;
}

// GAP events callback
static int ble_client_gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (rc != 0) return 0;

            if (!s_connecting && fields.name_len == 11 && memcmp(fields.name, "Curie-Robot", 11) == 0) {
                ESP_LOGI(TAG, "Robot found! Connecting...");
                ble_gap_disc_cancel();

                s_connecting = true;
                s_service_found = false;
                s_char_found = false;
                s_connect_started_us = esp_timer_get_time();
                rc = ble_gap_connect(own_addr_type, &event->disc.addr, 30000, NULL,
                                     ble_client_gap_event, NULL);
                if (rc != 0) {
                    s_connecting = false;
                    ESP_LOGE(TAG, "Failed to initiate connection: %d", rc);
                    ble_client_scan();
                }
            }
            return 0;
        }

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                s_connect_started_us = esp_timer_get_time();
                ESP_LOGI(TAG, "Successfully connected to robot! Starting service discovery...");
                int rc = ble_gattc_disc_all_svcs(event->connect.conn_handle, ble_client_on_disc_svc, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Failed to start service discovery: %d", rc);
                    ble_client_force_rescan("unable to start service discovery");
                }
            } else {
                s_recovery_inflight = false;
                clear_link_state();
                ESP_LOGW(TAG, "BLE connection failed: %d. Restarting scan...", event->connect.status);
                ble_client_scan();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Robot disconnected. Reason: %d. Re-scanning...", event->disconnect.reason);
            s_recovery_inflight = false;
            clear_link_state();
            ble_client_scan();
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (!s_connected && !s_connecting) {
                ESP_LOGW(TAG, "Scan completed without a robot; restarting scan");
                ble_client_scan();
            }
            return 0;

        default:
            return 0;
    }
}

static void ble_client_scan(void) {
    struct ble_gap_disc_params disc_params = {
        .filter_duplicates = 1,
        .passive = 0,
        .itvl = 128,
        .window = 64,
        .filter_policy = 0,
        .limited = 0,
    };

    ESP_LOGI(TAG, "Scanning for Curie-Robot advertisement...");
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, ble_client_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Error starting scan: %d", rc);
    }
}

static void ble_client_on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type: %d", rc);
        return;
    }
    ble_client_scan();
}

static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void queue_expression(uint8_t id) {
    s_pending_expression = id;
    s_current_expression_id = id;
    controller_ui_show_action(expression_label(id));
}

static void queue_expression_from_key(char key) {
    if (key == 'D') {
        s_expression_page ^= 1;
        ESP_LOGI(TAG, "Expression page toggled to %d", s_expression_page);
        controller_ui_show_action(s_expression_page ? "MORE FACES" : "MAIN FACES");
        return;
    }
    if (key == '*') {
        s_pending_button_latch |= CURIE_CTRL_BTN_BLINK;
        controller_ui_show_action("BLINK");
        return;
    }
    if (key == '#') {
        // Random expression between 1 and 12
        queue_expression((uint8_t)(esp_random() % 12 + 1));
        return;
    }
    if (key == '0') {
        queue_expression(13);
        return;
    }

    if (s_expression_page == 0) {
        switch (key) {
            case '1': queue_expression(1); break;
            case '2': queue_expression(2); break;
            case '3': queue_expression(3); break;
            case '4': queue_expression(4); break;
            case '5': queue_expression(5); break;
            case '6': queue_expression(6); break;
            case '7': queue_expression(7); break;
            case '8': queue_expression(8); break;
            case '9': queue_expression(9); break;
            case 'A': queue_expression(10); break;
            case 'B': queue_expression(11); break;
            case 'C': queue_expression(12); break;
        }
    } else {
        switch (key) {
            case '1': queue_expression(16); break;
            case '2': queue_expression(17); break;
            case '3': queue_expression(15); break;
            case '4': queue_expression(14); break;
            case '5': queue_expression(8); break;
            case '6': queue_expression(10); break;
            case '7': queue_expression(11); break;
            case '8': queue_expression(12); break;
            case '9': queue_expression(4); break;
            case 'A': queue_expression(5); break;
            case 'B': queue_expression(6); break;
            case 'C': queue_expression(7); break;
        }
    }
}

static void process_joystick_button(void) {
    bool pressed = (gpio_get_level(GPIO_NUM_25) == 0);
    uint8_t dpad = read_dpad_mask();
    int64_t now = esp_timer_get_time();

    if (pressed && !s_joy_button_down) {
        s_joy_button_down = true;
        s_joy_button_press_us = now;
        s_joy_long_handled = false;
        s_joy_combo_used = false;
    } else if (!pressed && s_joy_button_down) {
        if (!s_joy_long_handled && !s_joy_combo_used) {
            s_speed_mode = (s_speed_mode + 1) % 3;
            ESP_LOGI(TAG, "Speed mode changed to: %d", s_speed_mode);
            controller_ui_show_action(speed_label(s_speed_mode));
        }
        s_joy_button_down = false;
        s_joy_combo_used = false;
    }

    if (pressed && dpad != 0) {
        s_joy_combo_used = true;
        if (dpad & (1 << 0)) {
            s_pending_button_latch |= CURIE_CTRL_BTN_ARM_UP;
            controller_ui_show_action("ARMS UP");
        }
        if (dpad & (1 << 2)) {
            s_pending_button_latch |= CURIE_CTRL_BTN_ARM_DOWN;
            controller_ui_show_action("ARMS DOWN");
        }
        if (dpad & (1 << 3)) {
            s_pending_button_latch |= CURIE_CTRL_BTN_HOME;
            controller_ui_show_action("HOME");
        }
        if (dpad & (1 << 1)) {
            s_pending_button_latch |= CURIE_CTRL_BTN_ESTOP;
            controller_ui_show_action("ESTOP");
        }
        return;
    }

    if (pressed && !s_joy_long_handled && (now - s_joy_button_press_us) >= 1200000) {
        s_pending_button_latch |= CURIE_CTRL_BTN_CLEAR_ESTOP;
        s_joy_long_handled = true;
        ESP_LOGI(TAG, "Latched E-STOP Clear");
        controller_ui_show_action("ALL CLEAR");
    }
}

static uint8_t collect_buttons(void) {
    uint8_t buttons = s_pending_button_latch;
    s_pending_button_latch = 0;
    return buttons;
}

static int read_battery_percent(void) {
#if CONTROLLER_BATTERY_ADC_ENABLED
    int raw = 0;
    int millivolts = 0;
    adc_oneshot_read(s_adc_unit, ADC_CHANNEL_6, &raw);
    millivolts = (raw * 3300 * CONTROLLER_BATTERY_DIVIDER_X1000) / (4095 * 1000);
    if (millivolts < 3300) return 0;
    if (millivolts > 4200) return 100;
    return ((millivolts - 3300) * 100) / 900;
#else
    return -1;
#endif
}

static void main_loop_task(void *arg) {
    keypad_init();
    buttons_init();
    controller_ui_init();

    // Configure ADC oneshot unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_unit));

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_unit, ADC_CHANNEL_4, &chan_config)); // GPIO 32
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_unit, ADC_CHANNEL_5, &chan_config)); // GPIO 33
#if CONTROLLER_BATTERY_ADC_ENABLED
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_unit, ADC_CHANNEL_6, &chan_config)); // GPIO 34
#endif

    calibrate_joystick();
    ESP_LOGI(TAG, "Keypad, Buttons, and ADC initialized");

    while (1) {
        // 1. Process Keypad
        char key = keypad_get_key();
        if (key != '\0') {
            ESP_LOGI(TAG, "Key pressed: %c", key);
            queue_expression_from_key(key);
        }

        // 2. Process Joystick button
        process_joystick_button();
        log_dpad_state();

        // 3. Read Analog Axes
        int raw_x = 2048, raw_y = 2048;
        adc_oneshot_read(s_adc_unit, ADC_CHANNEL_4, &raw_x);
        adc_oneshot_read(s_adc_unit, ADC_CHANNEL_5, &raw_y);

        // 4. Send Packet if connected
        if (s_connected && s_chr_value_handle != 0) {
            uint8_t packet[8] = {0};
            packet[0] = CURIE_BLE_MAGIC;
            packet[1] = analog_to_axis_byte(raw_y, s_joy_center_y, true);  // Throttle
            packet[2] = analog_to_axis_byte(raw_x, s_joy_center_x, false); // Steering
            packet[3] = collect_buttons();
            packet[4] = s_pending_expression;
            packet[5] = s_speed_mode;
            packet[6] = s_sequence++;
            packet[7] = checksum_xor(packet);

            int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, s_chr_value_handle, packet, sizeof(packet));
            if (rc != 0) {
                s_packets_failed++;
                s_consecutive_write_errors++;
                ESP_LOGW(TAG, "BLE write failed rc=%d streak=%lu", rc, (unsigned long)s_consecutive_write_errors);
                if (rc == BLE_HS_ENOTCONN || s_consecutive_write_errors >= WRITE_ERROR_RECONNECT_THRESHOLD) {
                    ble_client_force_rescan("write path stalled");
                }
            } else {
                s_packets_sent++;
                s_consecutive_write_errors = 0;
                s_last_good_tx_us = esp_timer_get_time();
            }
            s_pending_expression = 0;
        }

        if (s_connecting && !s_connected && (esp_timer_get_time() - s_connect_started_us) > DISCOVERY_TIMEOUT_US) {
            ble_client_force_rescan("discovery/connect timeout");
        }

        log_controller_status();
        controller_ui_render(s_connected,
                             s_connecting,
                             expression_label(s_current_expression_id),
                             speed_label(s_speed_mode),
                             read_battery_percent());

        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}

void buttons_init(void) {
    gpio_num_t pins[] = {GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_25};
    for (int i = 0; i < 5; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << pins[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting Curie BLE Controller (Native C ESP-IDF)...");

    // Initialize NimBLE host port
    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_client_on_sync;

    // Start Main execution loop task
    xTaskCreate(main_loop_task, "main_loop_task", 4096, NULL, 5, NULL);

    // Run NimBLE host
    nimble_port_freertos_init(nimble_host_task);
}

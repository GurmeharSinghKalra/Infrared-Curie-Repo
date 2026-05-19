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

// Controller state variables
static uint8_t s_sequence = 0;
static uint8_t s_speed_mode = 1;
static uint8_t s_pending_expression = 0;
static uint8_t s_pending_button_latch = 0;
static uint8_t s_expression_page = 0;

static bool s_joy_button_down = false;
static int64_t s_joy_button_press_us = 0;
static bool s_joy_long_handled = false;

static uint32_t s_last_arm_repeat_ms = 0;
static uint32_t s_last_home_repeat_ms = 0;

// ADC handle
static adc_oneshot_unit_handle_t s_adc_unit = NULL;

static void ble_client_scan(void);
static void buttons_init(void);

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

static uint8_t analog_to_axis_byte(int raw, bool invert) {
    const int center = 2048;
    const int deadzone = 250;
    if (abs(raw - center) <= deadzone) {
        return 128;
    }
    int adjusted = (raw * 255) / 4095;
    if (invert) {
        adjusted = 255 - adjusted;
    }
    return clamp_to_byte(adjusted);
}

// Characteristic Discovery Callback
static int ble_client_on_disc_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr, void *arg) {
    if (error->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &gatt_svr_chr_uuid.u) == 0) {
            s_chr_value_handle = chr->val_handle;
            s_conn_handle = conn_handle;
            s_connected = true;
            ESP_LOGI(TAG, "BLE Curie service and characteristic discovered! Value Handle: %d", chr->val_handle);
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "BLE Characteristic discovery complete");
    } else {
        ESP_LOGE(TAG, "BLE Characteristic discovery error: %d", error->status);
    }
    return 0;
}

// Service Discovery Callback
static int ble_client_on_disc_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                  const struct ble_gatt_svc *svc, void *arg) {
    if (error->status == 0) {
        if (ble_uuid_cmp(&svc->uuid.u, &gatt_svr_svc_uuid.u) == 0) {
            ESP_LOGI(TAG, "Discovered Curie Service! Finding characteristics...");
            ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle,
                                    ble_client_on_disc_chr, NULL);
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "BLE Service discovery complete");
    } else {
        ESP_LOGE(TAG, "BLE Service discovery error: %d", error->status);
    }
    return 0;
}

// GAP events callback
static int ble_client_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (rc != 0) return 0;

            if (fields.name_len == 11 && memcmp(fields.name, "Curie-Robot", 11) == 0) {
                ESP_LOGI(TAG, "Robot found! Connecting...");
                ble_gap_disc_cancel();

                rc = ble_gap_connect(own_addr_type, &event->disc.addr, 30000, NULL,
                                     ble_client_gap_event, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Failed to initiate connection: %d", rc);
                    ble_client_scan();
                }
            }
            return 0;
        }

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Successfully connected to robot! Starting service discovery...");
                ble_gattc_disc_all_svcs(event->connect.conn_handle, ble_client_on_disc_svc, NULL);
            } else {
                ESP_LOGW(TAG, "BLE connection failed: %d. Restarting scan...", event->connect.status);
                ble_client_scan();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Robot disconnected. Reason: %d. Re-scanning...", event->disconnect.reason);
            s_connected = false;
            s_chr_value_handle = 0;
            s_conn_handle = 0;
            ble_client_scan();
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
}

static void queue_expression_from_key(char key) {
    if (key == 'D') {
        s_expression_page ^= 1;
        ESP_LOGI(TAG, "Expression page toggled to %d", s_expression_page);
        return;
    }
    if (key == '*') {
        s_pending_button_latch |= CURIE_CTRL_BTN_BLINK;
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
    int64_t now = esp_timer_get_time();

    if (pressed && !s_joy_button_down) {
        s_joy_button_down = true;
        s_joy_button_press_us = now;
        s_joy_long_handled = false;
    } else if (!pressed && s_joy_button_down) {
        if (!s_joy_long_handled) {
            s_speed_mode = (s_speed_mode + 1) % 3;
            ESP_LOGI(TAG, "Speed mode changed to: %d", s_speed_mode);
        }
        s_joy_button_down = false;
    }

    if (pressed && !s_joy_long_handled && (now - s_joy_button_press_us) >= 1200000) {
        s_pending_button_latch |= CURIE_CTRL_BTN_CLEAR_ESTOP;
        s_joy_long_handled = true;
        ESP_LOGI(TAG, "Latched E-STOP Clear");
    }
}

static uint8_t collect_buttons(void) {
    uint8_t buttons = s_pending_button_latch;
    s_pending_button_latch = 0;

    bool arm_up = (gpio_get_level(GPIO_NUM_26) == 0);
    bool arm_down = (gpio_get_level(GPIO_NUM_21) == 0);
    bool home = (gpio_get_level(GPIO_NUM_22) == 0);
    bool estop = (gpio_get_level(GPIO_NUM_27) == 0);

    if (estop) buttons |= CURIE_CTRL_BTN_ESTOP;

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (home && (now - s_last_home_repeat_ms >= 350)) {
        buttons |= CURIE_CTRL_BTN_HOME;
        s_last_home_repeat_ms = now;
    }

    if ((arm_up || arm_down) && (now - s_last_arm_repeat_ms >= 60)) {
        if (arm_up) buttons |= CURIE_CTRL_BTN_ARM_UP;
        if (arm_down) buttons |= CURIE_CTRL_BTN_ARM_DOWN;
        s_last_arm_repeat_ms = now;
    }

    return buttons;
}

static void main_loop_task(void *arg) {
    keypad_init();
    buttons_init();

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

        // 3. Read Analog Axes
        int raw_x = 2048, raw_y = 2048;
        adc_oneshot_read(s_adc_unit, ADC_CHANNEL_4, &raw_x);
        adc_oneshot_read(s_adc_unit, ADC_CHANNEL_5, &raw_y);

        // 4. Send Packet if connected
        if (s_connected && s_chr_value_handle != 0) {
            uint8_t packet[8] = {0};
            packet[0] = CURIE_BLE_MAGIC;
            packet[1] = analog_to_axis_byte(raw_y, true);  // Throttle
            packet[2] = analog_to_axis_byte(raw_x, false); // Steering
            packet[3] = collect_buttons();
            packet[4] = s_pending_expression;
            packet[5] = s_speed_mode;
            packet[6] = s_sequence++;
            packet[7] = checksum_xor(packet);

            int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, s_chr_value_handle, packet, sizeof(packet));
            if (rc != 0) {
                ESP_LOGE(TAG, "Error sending BLE flat write no-rsp: %d", rc);
            }
            s_pending_expression = 0;
        }

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

#include "telemetry.h"
#include "state/robot_state.h"
#include "events/event_bus.h"
#include "logging/ring_log.h"
#include "network/wifi_server.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <esp_http_server.h>

static const char *TAG = "TELEMETRY";

static httpd_handle_t s_ws_handle = NULL;
static int s_ws_fd = -1;
static SemaphoreHandle_t s_ws_mutex = NULL;

static void ensure_ws_mutex(void) {
    if (!s_ws_mutex) {
        s_ws_mutex = xSemaphoreCreateMutex();
        configASSERT(s_ws_mutex);
    }
}

void telemetry_register_ws(void *req_handle) {
    httpd_req_t *req = (httpd_req_t *)req_handle;
    if (!req) {
        return;
    }

    ensure_ws_mutex();
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    s_ws_handle = req->handle;
    s_ws_fd = httpd_req_to_sockfd(req);
    xSemaphoreGive(s_ws_mutex);
}

void telemetry_unregister_ws(void) {
    ensure_ws_mutex();
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    s_ws_handle = NULL;
    s_ws_fd = -1;
    xSemaphoreGive(s_ws_mutex);
}

esp_err_t telemetry_send_ws_text_from_req(void *req_handle, const char *text) {
    httpd_req_t *req = (httpd_req_t *)req_handle;
    if (!req || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    ensure_ws_mutex();
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };
    esp_err_t err = httpd_ws_send_frame(req, &ws_pkt);
    if (err != ESP_OK) {
        s_ws_handle = NULL;
        s_ws_fd = -1;
    }

    xSemaphoreGive(s_ws_mutex);
    return err;
}

esp_err_t telemetry_send_ws_text(const char *text) {
    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }

    ensure_ws_mutex();
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);

    if (!s_ws_handle || s_ws_fd < 0) {
        xSemaphoreGive(s_ws_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };
    esp_err_t err = httpd_ws_send_frame_async(s_ws_handle, s_ws_fd, &ws_pkt);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Telemetry websocket send failed: %s", esp_err_to_name(err));
        s_ws_handle = NULL;
        s_ws_fd = -1;
    }

    xSemaphoreGive(s_ws_mutex);
    return err;
}

static const char* dir_to_str(robot_dir_t d) {
    switch(d) {
        case DIR_FWD:       return "FWD";
        case DIR_BWD:       return "BWD";
        case DIR_LEFT:      return "LEFT";
        case DIR_RIGHT:     return "RIGHT";
        case DIR_FWD_LEFT:  return "FWD_LEFT";
        case DIR_FWD_RIGHT: return "FWD_RIGHT";
        case DIR_BWD_LEFT:  return "BWD_LEFT";
        case DIR_BWD_RIGHT: return "BWD_RIGHT";
        default:            return "STOP";
    }
}

static const char* exp_to_str(robot_expression_t e) {
    const char* names[] = {
        "neutral", "happy", "sad", "angry", "fear", "disgust",
        "confused", "contempt", "thoughtful", "shy", "funny",
        "surprised", "excited", "wink", "love", "sleep", "scan",
        "lost", "custom"
    };
    if (e >= 0 && e < EXP_COUNT) return names[e];
    return "happy";
}

static const char* state_to_str(robot_state_t s) {
    switch(s) {
        case ROBOT_STATE_IDLE:      return "IDLE";
        case ROBOT_STATE_MANUAL:    return "MANUAL";
        case ROBOT_STATE_DEMO:      return "DEMO";
        case ROBOT_STATE_ERROR:     return "ERROR";
        case ROBOT_STATE_LOW_POWER: return "LOW_POWER";
        default:                    return "UNKNOWN";
    }
}

void task_telemetry(void *arg) {
    ESP_LOGI(TAG, "Telemetry task started");
    ensure_ws_mutex();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        robot_snapshot_t snap = robot_state_snapshot();
        network_info_t net = wifi_server_get_info();

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "type", "telemetry");
        cJSON_AddStringToObject(json, "state", state_to_str(snap.state));
        cJSON_AddStringToObject(json, "dir", dir_to_str(snap.direction));
        cJSON_AddStringToObject(json, "expression", exp_to_str(snap.expression));
        cJSON_AddNumberToObject(json, "speed", snap.speed);
        cJSON_AddNumberToObject(json, "brightness", snap.mouth_brightness);
        cJSON_AddNumberToObject(json, "ls", snap.arms.ls);
        cJSON_AddNumberToObject(json, "le", snap.arms.le);
        cJSON_AddNumberToObject(json, "rs", snap.arms.rs);
        cJSON_AddNumberToObject(json, "re", snap.arms.re);
        cJSON_AddNumberToObject(json, "drive_left", snap.drive.left);
        cJSON_AddNumberToObject(json, "drive_right", snap.drive.right);
        cJSON_AddBoolToObject(json, "direct_drive", snap.direct_drive);
        cJSON_AddNumberToObject(json, "uptime", (double)((esp_timer_get_time() - snap.boot_time_us) / 1000000));
        cJSON_AddBoolToObject(json, "power", snap.power_on);
        cJSON_AddNumberToObject(json, "profile", snap.profile);
        cJSON_AddNumberToObject(json, "queue", event_bus_pending());

        cJSON_AddStringToObject(json, "wifi_mode", net.mode == NET_MODE_AP ? "AP" : "APSTA");
        cJSON_AddStringToObject(json, "wifi_ap_ssid", net.ap_ssid);
        cJSON_AddStringToObject(json, "wifi_ap_ip", net.ap_ip);
        cJSON_AddStringToObject(json, "wifi_sta_ssid", net.sta_ssid);
        cJSON_AddStringToObject(json, "wifi_sta_ip", net.sta_ip);
        cJSON_AddNumberToObject(json, "wifi_sta_rssi", net.sta_rssi);
        cJSON_AddBoolToObject(json, "wifi_sta_connected", net.sta_connected);
        cJSON_AddNumberToObject(json, "wifi_ap_clients", net.ap_clients);
        cJSON_AddStringToObject(json, "wifi_ssid", net.sta_connected ? net.sta_ssid : net.ap_ssid);
        cJSON_AddStringToObject(json, "wifi_ip", net.sta_connected ? net.sta_ip : net.ap_ip);
        cJSON_AddNumberToObject(json, "wifi_rssi", net.sta_connected ? net.sta_rssi : 0);

        if (snap.state == ROBOT_STATE_ERROR) {
            cJSON_AddNumberToObject(json, "error_code", snap.error_code);
        }

        char *str = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
        if (!str) {
            continue;
        }

        telemetry_send_ws_text(str);
        free(str);
    }
}

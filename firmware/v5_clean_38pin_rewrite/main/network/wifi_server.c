#include "wifi_server.h"
#include "events/event_bus.h"
#include "state/robot_state.h"
#include "telemetry/telemetry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

static const char *TAG = "WIFI_SRV";

#define AP_SSID "Curie Setup Open"
#define AP_PASSWORD ""
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

static httpd_handle_t s_server = NULL;
static esp_netif_t *s_ap_netif = NULL;
static char s_saved_ssid[33] = {0};
static char s_saved_pass[65] = {0};

static network_info_t s_net_info = {
    .mode = NET_MODE_AP,
    .ssid = AP_SSID,
    .ip = "192.168.4.1",
    .rssi = 0,
};

static void init_nvs_storage(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Resetting stale NVS state: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void load_wifi_credentials(void) {
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    size_t len = sizeof(s_saved_ssid);
    nvs_get_str(handle, "ssid", s_saved_ssid, &len);

    len = sizeof(s_saved_pass);
    nvs_get_str(handle, "pass", s_saved_pass, &len);
    nvs_close(handle);
}

static void save_wifi_credentials(const char *ssid, const char *pass) {
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_str(handle, "ssid", ssid);
    nvs_set_str(handle, "pass", pass ? pass : "");
    nvs_commit(handle);
    nvs_close(handle);

    strncpy(s_saved_ssid, ssid, sizeof(s_saved_ssid) - 1);
    s_saved_ssid[sizeof(s_saved_ssid) - 1] = '\0';
    strncpy(s_saved_pass, pass ? pass : "", sizeof(s_saved_pass) - 1);
    s_saved_pass[sizeof(s_saved_pass) - 1] = '\0';
}

network_info_t wifi_server_get_info(void) {
    return s_net_info;
}

static void set_cors(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
}

static esp_err_t send_json(httpd_req_t *req, cJSON *json) {
    char *body = cJSON_PrintUnformatted(json);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
}

static robot_dir_t str_to_dir(const char *value) {
    if (strcmp(value, "FWD") == 0) return DIR_FWD;
    if (strcmp(value, "BWD") == 0) return DIR_BWD;
    if (strcmp(value, "LEFT") == 0) return DIR_LEFT;
    if (strcmp(value, "RIGHT") == 0) return DIR_RIGHT;
    if (strcmp(value, "FWD_LEFT") == 0) return DIR_FWD_LEFT;
    if (strcmp(value, "FWD_RIGHT") == 0) return DIR_FWD_RIGHT;
    if (strcmp(value, "BWD_LEFT") == 0) return DIR_BWD_LEFT;
    if (strcmp(value, "BWD_RIGHT") == 0) return DIR_BWD_RIGHT;
    return DIR_STOP;
}

static robot_expression_t str_to_exp(const char *value) {
    if (strcmp(value, "happy") == 0) return EXP_HAPPY;
    if (strcmp(value, "neutral") == 0) return EXP_NEUTRAL;
    if (strcmp(value, "sad") == 0) return EXP_SAD;
    if (strcmp(value, "wink") == 0) return EXP_WINK;
    if (strcmp(value, "love") == 0) return EXP_LOVE;
    if (strcmp(value, "angry") == 0) return EXP_ANGRY;
    if (strcmp(value, "sleep") == 0) return EXP_SLEEP;
    if (strcmp(value, "scan") == 0) return EXP_SCAN;
    if (strcmp(value, "surprise") == 0) return EXP_SURPRISE;
    if (strcmp(value, "curious") == 0) return EXP_CURIOUS;
    if (strcmp(value, "excited") == 0) return EXP_EXCITED;
    if (strcmp(value, "confused") == 0) return EXP_CONFUSED;
    if (strcmp(value, "lost") == 0) return EXP_LOST;
    if (strcmp(value, "custom") == 0) return EXP_CUSTOM;
    return EXP_HAPPY;
}

static motion_profile_t str_to_profile(const char *value) {
    if (strcmp(value, "smooth") == 0 || strcmp(value, "Smooth") == 0 || strcmp(value, "Precision") == 0) {
        return PROFILE_SMOOTH;
    }
    if (strcmp(value, "aggressive") == 0 || strcmp(value, "Aggressive") == 0) {
        return PROFILE_AGGRESSIVE;
    }
    return PROFILE_NORMAL;
}

static void ws_send_ack_nack(httpd_req_t *req, const char *cmd, bool ack, const char *reason) {
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return;
    }

    if (ack) {
        cJSON_AddTrueToObject(json, "ack");
    } else {
        cJSON_AddTrueToObject(json, "nack");
        if (reason) {
            cJSON_AddStringToObject(json, "reason", reason);
        }
    }
    cJSON_AddStringToObject(json, "cmd", cmd);

    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) {
        return;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)body,
        .len = strlen(body),
    };
    httpd_ws_send_frame(req, &frame);
    free(body);
}

static esp_err_t index_get_handler(httpd_req_t *req) {
    set_cors(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

static esp_err_t style_get_handler(httpd_req_t *req) {
    set_cors(req);
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_send(req, (const char *)style_css_start, style_css_end - style_css_start);
}

static esp_err_t app_js_get_handler(httpd_req_t *req) {
    set_cors(req);
    httpd_resp_set_type(req, "application/javascript");
    return httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
}

static esp_err_t api_status_get_handler(httpd_req_t *req) {
    robot_snapshot_t snap = robot_state_snapshot();
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(json, "power", snap.power_on);
    cJSON_AddNumberToObject(json, "speed", snap.speed);
    cJSON_AddNumberToObject(json, "brightness", snap.mouth_brightness);
    cJSON_AddStringToObject(json, "mode", s_net_info.mode == NET_MODE_AP ? "AP" : "STA");
    cJSON_AddStringToObject(json, "ssid", s_net_info.ssid);
    cJSON_AddStringToObject(json, "ip", s_net_info.ip);

    esp_err_t err = send_json(req, json);
    cJSON_Delete(json);
    return err;
}

static esp_err_t api_wifi_scan_get_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };

    esp_err_t scan_err = esp_wifi_scan_start(&scan_config, true);
    if (scan_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan unavailable in AP build: %s", esp_err_to_name(scan_err));
        set_cors(req);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        set_cors(req);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!records) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_wifi_scan_get_ap_records(&ap_count, records);

    cJSON *array = cJSON_CreateArray();
    if (!array) {
        free(records);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    for (int i = 0; i < ap_count; i++) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry) {
            continue;
        }
        cJSON_AddStringToObject(entry, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(entry, "rssi", records[i].rssi);
        cJSON_AddBoolToObject(entry, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(array, entry);
    }

    free(records);
    esp_err_t err = send_json(req, array);
    cJSON_Delete(array);
    return err;
}

static esp_err_t api_wifi_connect_post_handler(httpd_req_t *req) {
    char body[256];
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, body, content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload read failed");
        return ESP_FAIL;
    }
    body[received] = '\0';

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *password = cJSON_GetObjectItem(json, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0') {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    save_wifi_credentials(ssid->valuestring, cJSON_IsString(password) ? password->valuestring : "");
    cJSON_Delete(json);

    cJSON *response = cJSON_CreateObject();
    if (!response) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(response, "status", "SAVED");
    cJSON_AddStringToObject(response, "message", "Credentials saved for later STA integration");
    cJSON_AddBoolToObject(response, "sta_enabled", false);

    esp_err_t err = send_json(req, response);
    cJSON_Delete(response);
    return err;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Dashboard websocket connected");
        telemetry_register_ws(req);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
    };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK || frame.len == 0) {
        return err;
    }

    uint8_t *payload_bytes = calloc(1, frame.len + 1);
    if (!payload_bytes) {
        return ESP_ERR_NO_MEM;
    }

    frame.payload = payload_bytes;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        free(payload_bytes);
        return err;
    }

    cJSON *json = cJSON_Parse((const char *)payload_bytes);
    free(payload_bytes);
    if (!json) {
        ws_send_ack_nack(req, "unknown", false, "Bad JSON");
        return ESP_OK;
    }

    cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
    if (!cJSON_IsString(cmd)) {
        ws_send_ack_nack(req, "unknown", false, "Missing cmd");
        cJSON_Delete(json);
        return ESP_OK;
    }

    const char *cmd_str = cmd->valuestring;
    event_payload_t payload = {0};
    bool queued = false;

    if (strcmp(cmd_str, "move") == 0) {
        cJSON *dir = cJSON_GetObjectItem(json, "dir");
        if (cJSON_IsString(dir)) {
            payload.direction = str_to_dir(dir->valuestring);
            queued = event_bus_send(EVT_MOVE, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "stop") == 0) {
        queued = event_bus_send_simple(EVT_STOP, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cmd_str, "estop") == 0) {
        queued = event_bus_send_simple(EVT_ESTOP, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cmd_str, "clear_error") == 0) {
        queued = event_bus_send_simple(EVT_CLEAR_ERROR, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cmd_str, "anim") == 0) {
        cJSON *animation = cJSON_GetObjectItem(json, "animation");
        if (cJSON_IsString(animation)) {
            payload.expression = str_to_exp(animation->valuestring);
            queued = event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "speed") == 0) {
        cJSON *speed = cJSON_GetObjectItem(json, "speed");
        if (cJSON_IsNumber(speed)) {
            payload.int_val = speed->valueint;
            queued = event_bus_send(EVT_SET_SPEED, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "brightness") == 0) {
        cJSON *brightness = cJSON_GetObjectItem(json, "brightness");
        if (cJSON_IsNumber(brightness)) {
            payload.int_val = brightness->valueint;
            queued = event_bus_send(EVT_SET_BRIGHTNESS, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "power") == 0) {
        cJSON *state = cJSON_GetObjectItem(json, "state");
        if (cJSON_IsString(state)) {
            payload.bool_val = strcmp(state->valuestring, "on") == 0;
            queued = event_bus_send(EVT_SET_POWER, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "profile") == 0 || strcmp(cmd_str, "set_profile") == 0) {
        cJSON *profile = cJSON_GetObjectItem(json, "profile");
        if (cJSON_IsString(profile)) {
            payload.profile = str_to_profile(profile->valuestring);
            queued = event_bus_send(EVT_SET_PROFILE, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "arms") == 0) {
        robot_arms_t current = robot_get_arms();
        cJSON *ls = cJSON_GetObjectItem(json, "ls");
        cJSON *le = cJSON_GetObjectItem(json, "le");
        cJSON *rs = cJSON_GetObjectItem(json, "rs");
        cJSON *re = cJSON_GetObjectItem(json, "re");
        payload.arms.ls = cJSON_IsNumber(ls) ? ls->valueint : current.ls;
        payload.arms.le = cJSON_IsNumber(le) ? le->valueint : current.le;
        payload.arms.rs = cJSON_IsNumber(rs) ? rs->valueint : current.rs;
        payload.arms.re = cJSON_IsNumber(re) ? re->valueint : current.re;
        queued = event_bus_send(EVT_SET_ARMS, EVT_SRC_WEBSOCKET, payload);
    } else if (strcmp(cmd_str, "set_arm") == 0) {
        cJSON *joint = cJSON_GetObjectItem(json, "joint");
        cJSON *value = cJSON_GetObjectItem(json, "value");
        if (cJSON_IsString(joint) && cJSON_IsNumber(value)) {
            robot_arms_t current = robot_get_arms();
            int angle = value->valueint;
            if (strcmp(joint->valuestring, "shoulder") == 0 ||
                strcmp(joint->valuestring, "base") == 0 ||
                strcmp(joint->valuestring, "left_shoulder") == 0) {
                current.ls = angle;
                current.rs = 180 - angle;
            } else if (strcmp(joint->valuestring, "right_shoulder") == 0) {
                current.rs = angle;
            }
            payload.arms = current;
            queued = event_bus_send(EVT_SET_ARMS, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "matrix") == 0) {
        cJSON *matrix = cJSON_GetObjectItem(json, "data");
        if (cJSON_IsArray(matrix) && cJSON_GetArraySize(matrix) == 16) {
            for (int i = 0; i < 16; i++) {
                cJSON *cell = cJSON_GetArrayItem(matrix, i);
                payload.mouth_data[i] = cJSON_IsNumber(cell) ? cell->valueint : 0;
            }
            queued = event_bus_send(EVT_SET_CUSTOM_MOUTH, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "blink") == 0) {
        queued = event_bus_send_simple(EVT_BLINK, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cmd_str, "demo") == 0) {
        queued = event_bus_send_simple(EVT_ENABLE_DEMO, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cmd_str, "set_mode") == 0) {
        cJSON *mode = cJSON_GetObjectItem(json, "mode");
        if (cJSON_IsString(mode)) {
            payload.target_state = strcmp(mode->valuestring, "manual") == 0 ? ROBOT_STATE_MANUAL : ROBOT_STATE_IDLE;
            queued = event_bus_send(EVT_CHANGE_STATE, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cmd_str, "reboot") == 0) {
        ws_send_ack_nack(req, cmd_str, true, NULL);
        cJSON_Delete(json);
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        return ESP_OK;
    } else {
        ws_send_ack_nack(req, cmd_str, false, "Unknown command");
        cJSON_Delete(json);
        return ESP_OK;
    }

    ws_send_ack_nack(req, cmd_str, queued, queued ? NULL : "Queue full");
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t ota_update_post_handler(httpd_req_t *req) {
    set_cors(req);

    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK(esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle));

    char buffer[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buffer, MIN(remaining, (int)sizeof(buffer)));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            esp_ota_end(ota_handle);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        ESP_ERROR_CHECK(esp_ota_write(ota_handle, buffer, received));
        remaining -= received;
    }

    ESP_ERROR_CHECK(esp_ota_end(ota_handle));
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(partition));

    httpd_resp_sendstr(req, "OTA Success! Robot rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static void start_http_server(void) {
    if (s_server) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    ESP_ERROR_CHECK(httpd_start(&s_server, &config));

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
    httpd_uri_t css_uri = { .uri = "/style.css", .method = HTTP_GET, .handler = style_get_handler };
    httpd_uri_t js_uri = { .uri = "/app.js", .method = HTTP_GET, .handler = app_js_get_handler };
    httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_get_handler };
    httpd_uri_t scan_uri = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = api_wifi_scan_get_handler };
    httpd_uri_t connect_uri = { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = api_wifi_connect_post_handler };
    httpd_uri_t ws_uri = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
    httpd_uri_t ota_uri = { .uri = "/update", .method = HTTP_POST, .handler = ota_update_post_handler };

    httpd_register_uri_handler(s_server, &index_uri);
    httpd_register_uri_handler(s_server, &css_uri);
    httpd_register_uri_handler(s_server, &js_uri);
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &connect_uri);
    httpd_register_uri_handler(s_server, &ws_uri);
    httpd_register_uri_handler(s_server, &ota_uri);

    ESP_LOGI(TAG, "HTTP server started at http://%s", s_net_info.ip);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP interface started");
                break;

            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP interface stopped");
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)data;
                ESP_LOGI(TAG, "Client connected to AP: " MACSTR ", AID=%d",
                         MAC2STR(event->mac), event->aid);
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)data;
                ESP_LOGI(TAG, "Client disconnected from AP: " MACSTR ", AID=%d, reason=%d",
                         MAC2STR(event->mac), event->aid, event->reason);
                break;
            }

            default:
                break;
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *event = (ip_event_ap_staipassigned_t *)data;
        ESP_LOGI(TAG, "AP assigned IP " IPSTR " to " MACSTR,
                 IP2STR(&event->ip), MAC2STR(event->mac));
    }
}

void start_wifi_server(void) {
    init_nvs_storage();
    load_wifi_credentials();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_ap_netif = esp_netif_create_default_wifi_ap();
    configASSERT(s_ap_netif);

    esp_netif_ip_info_t ip_info = {0};
    ESP_ERROR_CHECK(esp_netif_get_ip_info(s_ap_netif, &ip_info));
    snprintf(s_net_info.ip, sizeof(s_net_info.ip), IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "AP netif using default IP %s", s_net_info.ip);

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, NULL));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_config.ap.password, AP_PASSWORD, sizeof(ap_config.ap.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G));
    ESP_ERROR_CHECK(esp_wifi_config_11b_rate(WIFI_IF_AP, false));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_http_server();

    ESP_LOGI(TAG, "AP started: ssid=\"%s\" auth=OPEN ip=%s channel=%d protocol=11bg",
             AP_SSID, s_net_info.ip, AP_CHANNEL);
    if (s_saved_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Saved STA credentials retained for later integration: %s", s_saved_ssid);
    }
}

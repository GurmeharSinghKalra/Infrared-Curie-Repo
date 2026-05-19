#include "wifi_server.h"
#include "events/event_bus.h"
#include "telemetry/telemetry.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include <sys/param.h>

static const char *TAG = "WIFI_SRV";

#define WIFI_TX_POWER_DBM_X4 80
#define AP_SSID "CurieAPTest"
#define AP_PASSWORD ""
#define AP_IP_1 192
#define AP_IP_2 168
#define AP_IP_3 4
#define AP_IP_4 1
#define AP_CHANNEL 1

// Embedded Files from CMakeLists.txt
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

#define WIFI_RETRY_MAX 3
static int s_retry_num = 0;
static httpd_handle_t s_server = NULL;
static esp_timer_handle_t s_health_timer = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

static network_info_t s_net_info = {
    .mode = NET_MODE_AP,
    .ssid = "",
    .ip = "192.168.4.1",
    .rssi = 0
};

static char s_nvs_ssid[33] = {0};
static char s_nvs_pass[65] = {0};

static void apply_wifi_tx_power(void) {
    int8_t actual_power = 0;
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(WIFI_TX_POWER_DBM_X4));
    ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&actual_power));
    ESP_LOGI(TAG, "Wi-Fi TX power set request=%d (%.2f dBm), actual=%d (%.2f dBm)",
             WIFI_TX_POWER_DBM_X4, WIFI_TX_POWER_DBM_X4 / 4.0f,
             actual_power, actual_power / 4.0f);
}

static void ensure_ap_netif_config(void) {
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "AP netif is NULL; DHCP cannot run");
        return;
    }

    esp_netif_dhcp_status_t dhcps_status = ESP_NETIF_DHCP_STOPPED;
    ESP_ERROR_CHECK(esp_netif_dhcps_get_status(s_ap_netif, &dhcps_status));
    if (dhcps_status == ESP_NETIF_DHCP_STARTED) {
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
    }

    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, AP_IP_1, AP_IP_2, AP_IP_3, AP_IP_4);
    IP4_ADDR(&ip_info.gw, AP_IP_1, AP_IP_2, AP_IP_3, AP_IP_4);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));
    ESP_ERROR_CHECK(esp_netif_dhcps_get_status(s_ap_netif, &dhcps_status));

    snprintf(s_net_info.ip, sizeof(s_net_info.ip), IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "AP netif ready at %s/24, DHCP status=%s",
             s_net_info.ip,
             dhcps_status == ESP_NETIF_DHCP_STARTED ? "STARTED" : "STOPPED");
}

static void start_http_server(void);
static void stop_http_server(void);
static void start_ap_mode(void);
static void start_sta_mode(void);

static void init_nvs_storage(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init returned %s, erasing stale NVS data", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// =====================================================================
//  NVS CREDENTIALS
// =====================================================================

static void load_wifi_credentials(void) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_nvs_ssid);
        nvs_get_str(h, "ssid", s_nvs_ssid, &len);
        len = sizeof(s_nvs_pass);
        nvs_get_str(h, "pass", s_nvs_pass, &len);
        nvs_close(h);
    }
}

static void save_wifi_credentials(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_commit(h);
        nvs_close(h);
        strncpy(s_nvs_ssid, ssid, 32);
        strncpy(s_nvs_pass, pass, 64);
    }
}

// =====================================================================
//  NETWORK STATE ACCESS
// =====================================================================

network_info_t wifi_server_get_info(void) {
    // If STA, update RSSI
    if (s_net_info.mode == NET_MODE_STA) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_net_info.rssi = ap_info.rssi;
        } else {
            s_net_info.rssi = 0;
        }
    }
    return s_net_info;
}

// =====================================================================
//  HELPERS
// =====================================================================

static void set_cors(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static robot_dir_t str_to_dir(const char *s) {
    if (strcmp(s, "FWD") == 0) return DIR_FWD;
    if (strcmp(s, "BWD") == 0) return DIR_BWD;
    if (strcmp(s, "LEFT") == 0) return DIR_LEFT;
    if (strcmp(s, "RIGHT") == 0) return DIR_RIGHT;
    if (strcmp(s, "FWD_LEFT") == 0) return DIR_FWD_LEFT;
    if (strcmp(s, "FWD_RIGHT") == 0) return DIR_FWD_RIGHT;
    if (strcmp(s, "BWD_LEFT") == 0) return DIR_BWD_LEFT;
    if (strcmp(s, "BWD_RIGHT") == 0) return DIR_BWD_RIGHT;
    return DIR_STOP;
}

static robot_expression_t str_to_exp(const char *s) {
    if (strcmp(s, "happy") == 0)   return EXP_HAPPY;
    if (strcmp(s, "neutral") == 0) return EXP_NEUTRAL;
    if (strcmp(s, "sad") == 0)     return EXP_SAD;
    if (strcmp(s, "wink") == 0)    return EXP_WINK;
    if (strcmp(s, "love") == 0)    return EXP_LOVE;
    if (strcmp(s, "angry") == 0)   return EXP_ANGRY;
    if (strcmp(s, "sleep") == 0)   return EXP_SLEEP;
    if (strcmp(s, "scan") == 0)    return EXP_SCAN;
    if (strcmp(s, "surprise") == 0) return EXP_SURPRISE;
    if (strcmp(s, "curious") == 0)  return EXP_CURIOUS;
    if (strcmp(s, "excited") == 0)  return EXP_EXCITED;
    if (strcmp(s, "confused") == 0) return EXP_CONFUSED;
    if (strcmp(s, "lost") == 0)     return EXP_LOST;
    if (strcmp(s, "custom") == 0)  return EXP_CUSTOM;
    return EXP_HAPPY;
}

static void ws_send_ack_nack(httpd_req_t *req, const char *cmd, bool ack, const char *reason) {
    cJSON *resp = cJSON_CreateObject();
    if (ack) { cJSON_AddTrueToObject(resp, "ack"); cJSON_AddStringToObject(resp, "cmd", cmd); } 
    else { cJSON_AddTrueToObject(resp, "nack"); cJSON_AddStringToObject(resp, "cmd", cmd); if (reason) cJSON_AddStringToObject(resp, "reason", reason); }
    char *str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_ws_frame_t ws_pkt = { .final = true, .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)str, .len = strlen(str) };
    httpd_ws_send_frame(req, &ws_pkt);
    free(str);
}

// =====================================================================
//  HTTP STATIC FILES
// =====================================================================

static esp_err_t index_get_handler(httpd_req_t *req) { set_cors(req); httpd_resp_set_type(req, "text/html"); return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start); }
static esp_err_t style_get_handler(httpd_req_t *req) { set_cors(req); httpd_resp_set_type(req, "text/css"); return httpd_resp_send(req, (const char *)style_css_start, style_css_end - style_css_start); }
static esp_err_t app_js_get_handler(httpd_req_t *req) { set_cors(req); httpd_resp_set_type(req, "application/javascript"); return httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start); }

// =====================================================================
//  HTTP REST API ENDPOINTS (WiFi Provisioning)
// =====================================================================

static esp_err_t api_status_get_handler(httpd_req_t *req) {
    set_cors(req);
    robot_snapshot_t snap = robot_state_snapshot();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "power", snap.power_on);
    cJSON_AddNumberToObject(json, "speed", snap.speed);
    cJSON_AddNumberToObject(json, "brightness", snap.mouth_brightness);
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, HTTPD_RESP_USE_STRLEN);
    free(str); cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_wifi_scan_get_handler(httpd_req_t *req) {
    set_cors(req);
    wifi_scan_config_t scan_config = { .ssid = 0, .bssid = 0, .channel = 0, .show_hidden = false };
    
    // Graceful scan — do NOT use ESP_ERROR_CHECK (would reboot on failure in STA mode)
    esp_err_t scan_err = esp_wifi_scan_start(&scan_config, true);
    if (scan_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(scan_err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (!ap_records) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);

    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < ap_num; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_records[i].rssi);
        cJSON_AddBoolToObject(item, "secure", ap_records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(array, item);
    }
    free(ap_records);

    char *str = cJSON_PrintUnformatted(array);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, HTTPD_RESP_USE_STRLEN);
    free(str); cJSON_Delete(array);
    return ESP_OK;
}

static esp_err_t api_wifi_connect_post_handler(httpd_req_t *req) {
    set_cors(req);
    char buf[256];
    int remaining = req->content_len;
    if (remaining > sizeof(buf) - 1) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large"); return ESP_FAIL; }
    if (httpd_req_recv(req, buf, remaining) <= 0) return ESP_FAIL;
    buf[remaining] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    if (!json) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }
    
    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass = cJSON_GetObjectItem(json, "password");
    
    if (cJSON_IsString(ssid)) {
        const char *pw = cJSON_IsString(pass) ? pass->valuestring : "";
        save_wifi_credentials(ssid->valuestring, pw);
        
        httpd_resp_sendstr(req, "{\"status\":\"CONNECTING\"}");
        cJSON_Delete(json);
        
        // Trigger switch
        stop_http_server();
        start_sta_mode();
        return ESP_OK;
    }
    
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
    return ESP_FAIL;
}

// =====================================================================
//  WEBSOCKET HANDLER
// =====================================================================

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS Client connected");
        telemetry_register_ws(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) return ret;

    uint8_t *buf = calloc(1, ws_pkt.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    ws_pkt.payload = buf;
    
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) { free(buf); return ret; }

    cJSON *json = cJSON_Parse((char *)buf);
    free(buf);
    if (!json) { ws_send_ack_nack(req, "unknown", false, "Bad JSON"); return ESP_OK; }

    cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
    if (!cJSON_IsString(cmd)) { ws_send_ack_nack(req, "unknown", false, "Missing cmd"); cJSON_Delete(json); return ESP_OK; }

    const char* cstr = cmd->valuestring;
    event_payload_t payload = {0};
    bool queued = false;

    if (strcmp(cstr, "move") == 0) {
        cJSON *dir = cJSON_GetObjectItem(json, "dir");
        if (cJSON_IsString(dir)) { payload.direction = str_to_dir(dir->valuestring); queued = event_bus_send(EVT_MOVE, EVT_SRC_WEBSOCKET, payload); }
    } else if (strcmp(cstr, "stop") == 0) {
        queued = event_bus_send_simple(EVT_STOP, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cstr, "estop") == 0) {
        queued = event_bus_send_simple(EVT_ESTOP, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cstr, "clear_error") == 0) {
        queued = event_bus_send_simple(EVT_CLEAR_ERROR, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cstr, "anim") == 0) {
        cJSON *anim = cJSON_GetObjectItem(json, "animation");
        if (cJSON_IsString(anim)) { payload.expression = str_to_exp(anim->valuestring); queued = event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_WEBSOCKET, payload); }
    } else if (strcmp(cstr, "speed") == 0) {
        cJSON *spd = cJSON_GetObjectItem(json, "speed");
        if (cJSON_IsNumber(spd)) { payload.int_val = spd->valueint; queued = event_bus_send(EVT_SET_SPEED, EVT_SRC_WEBSOCKET, payload); }
    } else if (strcmp(cstr, "brightness") == 0) {
        cJSON *br = cJSON_GetObjectItem(json, "brightness");
        if (cJSON_IsNumber(br)) { payload.int_val = br->valueint; queued = event_bus_send(EVT_SET_BRIGHTNESS, EVT_SRC_WEBSOCKET, payload); }
    } else if (strcmp(cstr, "power") == 0) {
        cJSON *st = cJSON_GetObjectItem(json, "state");
        if (cJSON_IsString(st)) { payload.bool_val = (strcmp(st->valuestring, "on") == 0); queued = event_bus_send(EVT_SET_POWER, EVT_SRC_WEBSOCKET, payload); }
    } else if (strcmp(cstr, "profile") == 0 || strcmp(cstr, "set_profile") == 0) {
        cJSON *prof = cJSON_GetObjectItem(json, "profile");
        if (cJSON_IsString(prof)) {
            if (strcmp(prof->valuestring, "smooth") == 0 || strcmp(prof->valuestring, "Smooth") == 0 || strcmp(prof->valuestring, "Precision") == 0) payload.profile = PROFILE_SMOOTH;
            else if (strcmp(prof->valuestring, "aggressive") == 0 || strcmp(prof->valuestring, "Aggressive") == 0) payload.profile = PROFILE_AGGRESSIVE;
            else payload.profile = PROFILE_NORMAL;
            queued = event_bus_send(EVT_SET_PROFILE, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cstr, "arms") == 0) {
        cJSON *ls = cJSON_GetObjectItem(json, "ls"); cJSON *le = cJSON_GetObjectItem(json, "le");
        cJSON *rs = cJSON_GetObjectItem(json, "rs"); cJSON *re = cJSON_GetObjectItem(json, "re");
        robot_arms_t current = robot_get_arms();
        payload.arms.ls = cJSON_IsNumber(ls) ? ls->valueint : current.ls;
        payload.arms.le = cJSON_IsNumber(le) ? le->valueint : current.le;
        payload.arms.rs = cJSON_IsNumber(rs) ? rs->valueint : current.rs;
        payload.arms.re = cJSON_IsNumber(re) ? re->valueint : current.re;
        queued = event_bus_send(EVT_SET_ARMS, EVT_SRC_WEBSOCKET, payload);
    } else if (strcmp(cstr, "set_arm") == 0) {
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
    } else if (strcmp(cstr, "matrix") == 0) {
        cJSON *mat = cJSON_GetObjectItem(json, "data");
        if (cJSON_IsArray(mat) && cJSON_GetArraySize(mat) == 16) {
            for (int i=0; i<16; i++) payload.mouth_data[i] = cJSON_GetArrayItem(mat, i)->valueint;
            queued = event_bus_send(EVT_SET_CUSTOM_MOUTH, EVT_SRC_WEBSOCKET, payload);
        }
    } else if (strcmp(cstr, "blink") == 0) {
        queued = event_bus_send_simple(EVT_BLINK, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cstr, "demo") == 0) {
        queued = event_bus_send_simple(EVT_ENABLE_DEMO, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cstr, "set_mode") == 0) {
        queued = event_bus_send_simple(EVT_HEARTBEAT, EVT_SRC_WEBSOCKET);
    } else if (strcmp(cstr, "reboot") == 0) {
        ws_send_ack_nack(req, cstr, true, NULL);
        cJSON_Delete(json);
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        return ESP_OK;
    } else {
        ws_send_ack_nack(req, cstr, false, "Unknown command"); cJSON_Delete(json); return ESP_OK;
    }

    ws_send_ack_nack(req, cstr, queued, queued ? NULL : "Queue full");
    cJSON_Delete(json);
    return ESP_OK;
}

// =====================================================================
//  OTA HANDLER
// =====================================================================

static esp_err_t ota_update_post_handler(httpd_req_t *req) {
    set_cors(req);
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) { httpd_resp_send_500(req); return ESP_FAIL; }

    char buf[1024]; int received; int remaining = req->content_len;
    while (remaining > 0) {
        if ((received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_end(update_handle); httpd_resp_send_500(req); return ESP_FAIL;
        }
        err = esp_ota_write(update_handle, (const void *)buf, received);
        if (err != ESP_OK) { esp_ota_end(update_handle); httpd_resp_send_500(req); return ESP_FAIL; }
        remaining -= received;
    }
    if (esp_ota_end(update_handle) != ESP_OK || esp_ota_set_boot_partition(update_partition) != ESP_OK) {
        httpd_resp_send_500(req); return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "OTA Success! Robot rebooting...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

// =====================================================================
//  HTTP SERVER CONTROL
// =====================================================================

static void start_http_server(void) {
    if (s_server) return;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    
    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_uri_t get_idx  = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
        httpd_uri_t get_css  = { .uri = "/style.css", .method = HTTP_GET, .handler = style_get_handler };
        httpd_uri_t get_js   = { .uri = "/app.js", .method = HTTP_GET, .handler = app_js_get_handler };
        httpd_uri_t get_st   = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_get_handler };
        httpd_uri_t get_scan = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = api_wifi_scan_get_handler };
        httpd_uri_t post_con = { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = api_wifi_connect_post_handler };
        httpd_uri_t ws       = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
        httpd_uri_t post_ota = { .uri = "/update", .method = HTTP_POST, .handler = ota_update_post_handler };
        
        httpd_register_uri_handler(s_server, &get_idx);
        httpd_register_uri_handler(s_server, &get_css);
        httpd_register_uri_handler(s_server, &get_js);
        httpd_register_uri_handler(s_server, &get_st);
        httpd_register_uri_handler(s_server, &get_scan);
        httpd_register_uri_handler(s_server, &post_con);
        httpd_register_uri_handler(s_server, &ws);
        httpd_register_uri_handler(s_server, &post_ota);
        
        ESP_LOGI(TAG, "HTTP Server started");
    }
}

static void stop_http_server(void) {
    if (s_server) {
        // Null out the telemetry WS handle BEFORE stopping the server
        // to prevent use-after-free in the telemetry task
        telemetry_register_ws(NULL);
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP Server stopped");
    }
}

// =====================================================================
//  WIFI EVENT HANDLER
// =====================================================================

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP interface started");
                break;
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP interface stopped");
                break;
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "STA Disconnected");
                if (s_retry_num < WIFI_RETRY_MAX) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "Retrying connection... (%d/%d)", s_retry_num, WIFI_RETRY_MAX);
                } else {
                    ESP_LOGE(TAG, "Connection failed. Falling back to AP mode");
                    s_net_info.rssi = 0;
                    strcpy(s_net_info.ip, "0.0.0.0");
                    event_bus_send_simple(EVT_STOP, EVT_SRC_WIFI_EVENT);
                    stop_http_server();
                    start_ap_mode();
                }
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)data;
                ESP_LOGI(TAG, "Client connected to AP: " MACSTR ", AID=%d", MAC2STR(event->mac), event->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)data;
                ESP_LOGI(TAG, "Client disconnected from AP: " MACSTR ", AID=%d, reason=%d",
                         MAC2STR(event->mac), event->aid, event->reason);
                // Don't stop motors on AP client disconnect — too aggressive.
                // The robot should keep running if a phone screen locks.
                break;
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) data;
        sprintf(s_net_info.ip, IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        ESP_LOGI(TAG, "Got IP: %s", s_net_info.ip);
        s_net_info.mode = NET_MODE_STA;
        strncpy(s_net_info.ssid, s_nvs_ssid, 32);
        
        // Start HTTP server on the new IP
        start_http_server();
    } else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *event = (ip_event_ap_staipassigned_t *)data;
        ESP_LOGI(TAG, "AP assigned IP " IPSTR " to " MACSTR,
                 IP2STR(&event->ip), MAC2STR(event->mac));
    }
}

// =====================================================================
//  WIFI MODE SWITCHING
// =====================================================================

static void start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting AP Mode");
    ESP_LOGI(TAG, "s_ap_netif=%p", s_ap_netif);
    s_net_info.mode = NET_MODE_AP;
    strcpy(s_net_info.ip, "192.168.4.1");
    strcpy(s_net_info.ssid, AP_SSID);
    s_net_info.rssi = 0;

    wifi_config_t wifi_config_ap = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
    ensure_ap_netif_config();
    ESP_LOGI(TAG, "AP started: ssid=\"%s\" auth=OPEN ip=%s channel=%d bw=HT20",
             AP_SSID, s_net_info.ip, AP_CHANNEL);
    ESP_LOGI(TAG, "HTTP server intentionally skipped for DHCP-only AP test");
}

static void start_sta_mode(void) {
    ESP_LOGI(TAG, "Starting STA Mode for SSID: %s", s_nvs_ssid);
    s_retry_num = 0;
    
    esp_wifi_stop();
    wifi_config_t wifi_config_sta = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .capable = true, .required = false },
        },
    };
    strncpy((char *)wifi_config_sta.sta.ssid, s_nvs_ssid, 32);
    strncpy((char *)wifi_config_sta.sta.password, s_nvs_pass, 64);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    apply_wifi_tx_power();
}

// =====================================================================
//  HEALTH CHECK TIMER
// =====================================================================

static void health_check_cb(void *arg) {
    // If in STA mode but disconnected, auto-fallback
    if (s_net_info.mode == NET_MODE_STA) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
            ESP_LOGW(TAG, "Health check failed. Forcing reconnect/fallback.");
            esp_wifi_disconnect(); // Triggers event handler
        }
    }
}

// =====================================================================
//  PUBLIC INIT
// =====================================================================

void start_wifi_server(void) {
    init_nvs_storage();
    load_wifi_credentials();
    ESP_LOGI(TAG, "Stored STA SSID length=%u; forcing AP-only DHCP test build",
             (unsigned)strlen(s_nvs_ssid));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_LOGI(TAG, "Created default AP netif: %p", s_ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, NULL));

    start_ap_mode();

    // Start 15-second health check timer
    esp_timer_create_args_t timer_cfg = { .callback = &health_check_cb, .name = "health_check" };
    ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &s_health_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_health_timer, 15000000));
}

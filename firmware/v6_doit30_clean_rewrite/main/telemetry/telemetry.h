#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// Telemetry task — formats current state into JSON and pushes
// via WebSocket every 500ms. Also handles log dump requests.
void task_telemetry(void *arg);

// Used by wifi_server to register the active websocket handle
void telemetry_register_ws(void *req_handle);
void telemetry_unregister_ws(void);
esp_err_t telemetry_send_ws_text(const char *text);
esp_err_t telemetry_send_ws_text_from_req(void *req_handle, const char *text);

#ifdef __cplusplus
}
#endif

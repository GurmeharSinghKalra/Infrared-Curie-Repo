#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Telemetry task — formats current state into JSON and pushes
// via WebSocket every 500ms. Also handles log dump requests.
void task_telemetry(void *arg);

// Used by wifi_server to register the active websocket handle
void telemetry_register_ws(void *req_handle);

#ifdef __cplusplus
}
#endif

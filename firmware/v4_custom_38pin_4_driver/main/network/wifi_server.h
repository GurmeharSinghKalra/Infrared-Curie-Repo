#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Initialize the WiFi Access Point and HTTP/WebSocket server.
// Converts incoming network traffic into events and posts them to the Event Bus.
// Provides endpoints for OTA, dashboard files, and APIs.
void start_wifi_server(void);

// =====================================================================
//  WIFI PROVISIONING TYPES
// =====================================================================

typedef enum {
    NET_MODE_AP,
    NET_MODE_STA
} network_mode_t;

typedef struct {
    network_mode_t mode;
    char ssid[33];
    char ip[16];
    int rssi;
} network_info_t;

// Get the current network status (used by telemetry)
network_info_t wifi_server_get_info(void);

#ifdef __cplusplus
}
#endif

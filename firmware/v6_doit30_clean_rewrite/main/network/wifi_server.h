#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void start_wifi_server(void);

typedef enum {
    NET_MODE_AP,
    NET_MODE_APSTA
} network_mode_t;

typedef struct {
    network_mode_t mode;
    char ap_ssid[33];
    char ap_ip[16];
    char sta_ssid[33];
    char sta_ip[16];
    int sta_rssi;
    int ap_clients;
    bool sta_connected;
} network_info_t;

network_info_t wifi_server_get_info(void);

#ifdef __cplusplus
}
#endif

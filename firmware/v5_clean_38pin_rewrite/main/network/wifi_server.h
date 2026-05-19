#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void start_wifi_server(void);

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

network_info_t wifi_server_get_info(void);

#ifdef __cplusplus
}
#endif

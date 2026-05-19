#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Packet format matches the original ESP-NOW format exactly.
#define CURIE_BLE_PACKET_SIZE 8

void start_ble_controller(void);

#ifdef __cplusplus
}
#endif

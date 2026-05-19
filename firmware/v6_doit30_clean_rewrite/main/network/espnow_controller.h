#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CURIE_ESPNOW_PACKET_SIZE 8
#define CURIE_ESPNOW_MAGIC 0xC4

typedef enum {
    CURIE_CTRL_BTN_ESTOP       = 1 << 0,
    CURIE_CTRL_BTN_HOME        = 1 << 1,
    CURIE_CTRL_BTN_ARM_UP      = 1 << 2,
    CURIE_CTRL_BTN_ARM_DOWN    = 1 << 3,
    CURIE_CTRL_BTN_CLEAR_ESTOP = 1 << 4,
    CURIE_CTRL_BTN_BLINK       = 1 << 5,
} curie_ctrl_button_t;

// Packet v1:
// byte 0: CURIE_ESPNOW_MAGIC
// byte 1: throttle joystick, 0=forward, 128=center, 255=backward
// byte 2: steering joystick, 0=left, 128=center, 255=right
// byte 3: button bitfield, see curie_ctrl_button_t
// byte 4: expression id, 0=no change, 1-17=set expression
// byte 5: speed mode, 0=40%, 1=70%, 2=100%
// byte 6: sequence number
// byte 7: XOR checksum of bytes 0-6
void start_espnow_controller(void);

#ifdef __cplusplus
}
#endif

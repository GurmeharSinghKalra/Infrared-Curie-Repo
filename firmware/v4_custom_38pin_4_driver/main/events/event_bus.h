#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "state/robot_state.h"

typedef enum {
    EVT_MOVE,
    EVT_SET_DRIVE,
    EVT_SET_SPEED,
    EVT_STOP,

    EVT_SET_EXPRESSION,
    EVT_SET_BRIGHTNESS,
    EVT_SET_CUSTOM_MOUTH,
    EVT_BLINK,

    EVT_SET_ARMS,

    EVT_SET_POWER,
    EVT_CHANGE_STATE,
    EVT_SET_PROFILE,
    EVT_ENABLE_DEMO,
    EVT_ESTOP,
    EVT_CLEAR_ERROR,

    EVT_HEARTBEAT,
    EVT_LOG_REQUEST,

    EVT_COUNT
} event_type_t;

typedef enum {
    EVT_SRC_WEBSOCKET,
    EVT_SRC_HTTP,
    EVT_SRC_ESPNOW,
    EVT_SRC_INTERNAL,
    EVT_SRC_WIFI_EVENT
} event_source_t;

typedef union {
    robot_dir_t         direction;
    robot_expression_t  expression;
    motion_profile_t    profile;
    robot_state_t       target_state;
    robot_arms_t        arms;
    robot_drive_t       drive;
    int                 int_val;
    bool                bool_val;
    uint8_t             mouth_data[16];
} event_payload_t;

typedef struct {
    event_type_t    type;
    event_source_t  source;
    int64_t         timestamp_us;
    event_payload_t payload;
} robot_event_t;

void event_bus_init(void);
bool event_bus_post(const robot_event_t *event);
bool event_bus_send(event_type_t type, event_source_t source, event_payload_t payload);
bool event_bus_send_simple(event_type_t type, event_source_t source);
bool event_bus_receive(robot_event_t *out, uint32_t timeout_ms);
int event_bus_pending(void);
const char *event_type_name(event_type_t type);

#ifdef __cplusplus
}
#endif

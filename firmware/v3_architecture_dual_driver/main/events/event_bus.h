#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "state/robot_state.h"

// =====================================================================
//  EVENT TYPES
// =====================================================================

typedef enum {
    // Motion events
    EVT_MOVE,               // payload: direction
    EVT_SET_SPEED,          // payload: speed (0-100)
    EVT_STOP,               // no payload

    // Expression events
    EVT_SET_EXPRESSION,     // payload: expression enum
    EVT_SET_BRIGHTNESS,     // payload: brightness (0-15)
    EVT_SET_CUSTOM_MOUTH,   // payload: 16-byte matrix
    EVT_BLINK,              // no payload

    // Arm events
    EVT_SET_ARMS,           // payload: arms struct

    // System events
    EVT_SET_POWER,          // payload: bool on/off
    EVT_CHANGE_STATE,       // payload: target state
    EVT_SET_PROFILE,        // payload: motion profile
    EVT_ENABLE_DEMO,        // no payload — toggle demo
    EVT_CLEAR_ERROR,        // no payload

    // Diagnostics
    EVT_HEARTBEAT,          // no payload — keepalive
    EVT_LOG_REQUEST,        // no payload — request log dump

    EVT_COUNT               // sentinel
} event_type_t;

// =====================================================================
//  EVENT SOURCE
// =====================================================================

typedef enum {
    EVT_SRC_WEBSOCKET,
    EVT_SRC_HTTP,
    EVT_SRC_INTERNAL,       // decision engine, demo mode, etc.
    EVT_SRC_WIFI_EVENT      // disconnect handler
} event_source_t;

// =====================================================================
//  EVENT PAYLOAD UNION
// =====================================================================

typedef union {
    robot_dir_t         direction;
    robot_expression_t  expression;
    motion_profile_t    profile;
    robot_state_t       target_state;
    robot_arms_t        arms;
    int                 int_val;        // speed, brightness
    bool                bool_val;       // power on/off
    uint8_t             mouth_data[16]; // custom matrix
} event_payload_t;

// =====================================================================
//  EVENT STRUCTURE
// =====================================================================

typedef struct {
    event_type_t    type;
    event_source_t  source;
    int64_t         timestamp_us;   // esp_timer_get_time() at creation
    event_payload_t payload;
} robot_event_t;

// =====================================================================
//  EVENT BUS API
// =====================================================================

// Initialize the event queue (call once from app_main)
void event_bus_init(void);

// Post an event to the queue. Returns true if queued, false if full.
bool event_bus_post(const robot_event_t *event);

// Convenience: build and post an event in one call
bool event_bus_send(event_type_t type, event_source_t source, event_payload_t payload);

// Convenience: post a simple event with no meaningful payload
bool event_bus_send_simple(event_type_t type, event_source_t source);

// Block until an event arrives (called by decision engine)
bool event_bus_receive(robot_event_t *out, uint32_t timeout_ms);

// Get current queue depth (for telemetry)
int event_bus_pending(void);

// Get the string name of an event type (for logging)
const char *event_type_name(event_type_t type);

#ifdef __cplusplus
}
#endif

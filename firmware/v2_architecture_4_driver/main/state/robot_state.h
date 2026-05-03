#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// =====================================================================
//  ROBOT STATE MACHINE
// =====================================================================

typedef enum {
    ROBOT_STATE_IDLE,       // Powered on, waiting for commands
    ROBOT_STATE_MANUAL,     // Actively receiving user commands
    ROBOT_STATE_DEMO,       // Running scripted demo sequence
    ROBOT_STATE_ERROR,      // Fault detected — motors locked out
    ROBOT_STATE_LOW_POWER   // Reduced functionality to conserve power
} robot_state_t;

// =====================================================================
//  DIRECTION & EXPRESSION ENUMS (preserved from V1)
// =====================================================================

typedef enum {
    DIR_STOP,
    DIR_FWD,
    DIR_BWD,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_FWD_LEFT,
    DIR_FWD_RIGHT,
    DIR_BWD_LEFT,
    DIR_BWD_RIGHT
} robot_dir_t;

typedef enum {
    EXP_HAPPY,      // ◠‿◠  — Round eyes, wide smile
    EXP_NEUTRAL,    // •_•  — Simple dots, flat mouth
    EXP_SAD,        // ╥﹏╥  — Droopy eyes, frown
    EXP_WINK,       // •‿-  — One eye closed, smirk
    EXP_LOVE,       // ♥‿♥  — Heart eyes, big smile
    EXP_ANGRY,      // >_<  — Furrowed slashes, teeth
    EXP_SLEEP,      // -_-  — Closed lines, zzz
    EXP_SCAN,       // ◉_◉  — Wide circles, O mouth
    EXP_CUSTOM      // User-drawn matrix data
} robot_expression_t;

// =====================================================================
//  MOTION PROFILES
// =====================================================================

typedef enum {
    PROFILE_SMOOTH,         // Ramp step 4 — gentle acceleration
    PROFILE_NORMAL,         // Ramp step 8 — balanced (V1 default)
    PROFILE_AGGRESSIVE      // Ramp step 20 — snappy response
} motion_profile_t;

// =====================================================================
//  ARM STATE
// =====================================================================

typedef struct {
    int ls;     // Left Shoulder  (0-180)
    int le;     // Left Elbow     (0-180)
    int rs;     // Right Shoulder (0-180)
    int re;     // Right Elbow    (0-180)
} robot_arms_t;

// =====================================================================
//  SHARED ROBOT SNAPSHOT (read-only copy for telemetry/display)
// =====================================================================

typedef struct {
    robot_state_t       state;
    robot_dir_t         direction;
    robot_expression_t  expression;
    motion_profile_t    profile;
    robot_arms_t        arms;
    int                 speed;          // 0-100
    int                 mouth_brightness; // 0-15
    bool                power_on;
    int64_t             boot_time_us;
    uint32_t            error_code;
} robot_snapshot_t;

// =====================================================================
//  STATE MACHINE API
// =====================================================================

// Initialize the state system (creates mutex, sets IDLE)
void robot_state_init(void);

// Attempt a state transition. Returns true if accepted, false if rejected.
// Enforces valid transitions (e.g., cannot go from ERROR to DEMO).
bool robot_state_transition(robot_state_t new_state);

// Get current state (thread-safe)
robot_state_t robot_state_get(void);

// Set error with code (forces ERROR state)
void robot_state_set_error(uint32_t error_code);

// Clear error (transitions back to IDLE)
bool robot_state_clear_error(void);

// Get a full snapshot of the current robot state (thread-safe copy)
robot_snapshot_t robot_state_snapshot(void);

// =====================================================================
//  SHARED STATE ACCESSORS (used by decision engine only)
// =====================================================================

// The decision engine is the ONLY module that calls these.
void robot_set_direction(robot_dir_t dir);
void robot_set_speed(int speed);
void robot_set_expression(robot_expression_t exp);
void robot_set_profile(motion_profile_t profile);
void robot_set_arms(robot_arms_t arms);
void robot_set_power(bool on);
void robot_set_brightness(int brightness);
void robot_set_custom_mouth(const uint8_t data[16]);
void robot_set_force_blink(bool blink);

// Getters for task consumers
robot_dir_t         robot_get_direction(void);
int                 robot_get_speed(void);
robot_expression_t  robot_get_expression(void);
motion_profile_t    robot_get_profile(void);
robot_arms_t        robot_get_arms(void);
bool                robot_get_power(void);
int                 robot_get_brightness(void);
void                robot_get_custom_mouth(uint8_t out[16]);
bool                robot_get_and_clear_blink(void);

#ifdef __cplusplus
}
#endif

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
    ROBOT_STATE_IDLE,
    ROBOT_STATE_MANUAL,
    ROBOT_STATE_DEMO,
    ROBOT_STATE_ERROR,
    ROBOT_STATE_LOW_POWER
} robot_state_t;

// =====================================================================
//  DIRECTION, DRIVE, AND EXPRESSION TYPES
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
    EXP_NEUTRAL,
    EXP_HAPPY,
    EXP_SAD,
    EXP_ANGRY,
    EXP_FEAR,
    EXP_DISGUST,
    EXP_CONFUSED,
    EXP_CONTEMPT,
    EXP_THOUGHTFUL,
    EXP_SHY,
    EXP_FUNNY,
    EXP_SURPRISED,
    EXP_EXCITED,
    EXP_WINK,
    EXP_LOVE,
    EXP_SLEEP,
    EXP_SCAN,
    EXP_LOST,
    EXP_CUSTOM,
    EXP_COUNT
} robot_expression_t;

typedef struct {
    int left;    // Left tank side, -100 to 100.
    int right;   // Right tank side, -100 to 100.
} robot_drive_t;

// =====================================================================
//  MOTION PROFILES
// =====================================================================

typedef enum {
    PROFILE_SMOOTH,
    PROFILE_NORMAL,
    PROFILE_AGGRESSIVE
} motion_profile_t;

// =====================================================================
//  ARM STATE
// =====================================================================

typedef struct {
    int ls;     // Left shoulder (0-180).
    int le;     // Reserved legacy elbow field.
    int rs;     // Right shoulder (0-180).
    int re;     // Reserved legacy elbow field.
} robot_arms_t;

// =====================================================================
//  SHARED ROBOT SNAPSHOT
// =====================================================================

typedef struct {
    robot_state_t       state;
    robot_dir_t         direction;
    robot_expression_t  expression;
    motion_profile_t    profile;
    robot_arms_t        arms;
    robot_drive_t       drive;
    int                 speed;             // 0-100.
    int                 mouth_brightness;  // 0-15.
    bool                power_on;
    bool                direct_drive;
    int64_t             boot_time_us;
    uint32_t            error_code;
} robot_snapshot_t;

// =====================================================================
//  STATE MACHINE API
// =====================================================================

void robot_state_init(void);
bool robot_state_transition(robot_state_t new_state);
robot_state_t robot_state_get(void);
void robot_state_set_error(uint32_t error_code);
bool robot_state_clear_error(void);
robot_snapshot_t robot_state_snapshot(void);

// =====================================================================
//  SHARED STATE ACCESSORS
// =====================================================================

void robot_set_direction(robot_dir_t dir);
void robot_set_direct_drive(robot_drive_t drive);
void robot_set_speed(int speed);
void robot_set_expression(robot_expression_t exp);
void robot_set_profile(motion_profile_t profile);
void robot_set_arms(robot_arms_t arms);
void robot_set_power(bool on);
void robot_set_brightness(int brightness);
void robot_set_custom_mouth(const uint8_t data[16]);
void robot_set_force_blink(bool blink);

robot_dir_t         robot_get_direction(void);
robot_drive_t       robot_get_drive(void);
bool                robot_get_direct_drive(void);
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

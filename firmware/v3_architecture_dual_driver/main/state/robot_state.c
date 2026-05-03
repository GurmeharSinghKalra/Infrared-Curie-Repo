#include "robot_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "STATE";

// =====================================================================
//  INTERNAL PROTECTED STATE
// =====================================================================

static SemaphoreHandle_t s_mutex = NULL;

static struct {
    robot_state_t       state;
    robot_dir_t         direction;
    robot_expression_t  expression;
    motion_profile_t    profile;
    robot_arms_t        arms;
    int                 speed;
    int                 mouth_brightness;
    bool                power_on;
    bool                force_blink;
    int64_t             boot_time_us;
    uint32_t            error_code;
    uint8_t             custom_mouth[16];
} s_robot;

// =====================================================================
//  STATE MACHINE INIT
// =====================================================================

void robot_state_init(void) {
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    memset(&s_robot, 0, sizeof(s_robot));
    s_robot.state           = ROBOT_STATE_IDLE;
    s_robot.direction       = DIR_STOP;
    s_robot.expression      = EXP_HAPPY;
    s_robot.profile         = PROFILE_NORMAL;
    s_robot.arms            = (robot_arms_t){90, 45, 90, 45};
    s_robot.speed           = 80;
    s_robot.mouth_brightness = 8;
    s_robot.power_on        = true;
    s_robot.boot_time_us    = esp_timer_get_time();

    ESP_LOGI(TAG, "State machine initialized → IDLE");
}

// =====================================================================
//  STATE TRANSITIONS (with validation)
// =====================================================================

// Transition table: which transitions are legal
static bool is_valid_transition(robot_state_t from, robot_state_t to) {
    // ERROR can only go to IDLE (via clear_error)
    if (from == ROBOT_STATE_ERROR && to != ROBOT_STATE_IDLE) return false;

    // DEMO can only be entered from IDLE
    if (to == ROBOT_STATE_DEMO && from != ROBOT_STATE_IDLE) return false;

    // Cannot enter ERROR via transition (use set_error instead)
    // But we allow it internally
    if (to == ROBOT_STATE_ERROR) return true;

    // LOW_POWER can be entered from IDLE or MANUAL
    if (to == ROBOT_STATE_LOW_POWER && from != ROBOT_STATE_IDLE && from != ROBOT_STATE_MANUAL) return false;

    return true;
}

bool robot_state_transition(robot_state_t new_state) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    robot_state_t old = s_robot.state;

    if (!is_valid_transition(old, new_state)) {
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "Rejected transition %d → %d", old, new_state);
        return false;
    }

    s_robot.state = new_state;

    // Side effects of state changes
    if (new_state == ROBOT_STATE_ERROR || new_state == ROBOT_STATE_IDLE) {
        s_robot.direction = DIR_STOP;
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "State: %d → %d", old, new_state);
    return true;
}

robot_state_t robot_state_get(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    robot_state_t s = s_robot.state;
    xSemaphoreGive(s_mutex);
    return s;
}

void robot_state_set_error(uint32_t error_code) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.state = ROBOT_STATE_ERROR;
    s_robot.error_code = error_code;
    s_robot.direction = DIR_STOP;
    xSemaphoreGive(s_mutex);
    ESP_LOGE(TAG, "ERROR state forced, code=0x%08lx", (unsigned long)error_code);
}

bool robot_state_clear_error(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_robot.state != ROBOT_STATE_ERROR) {
        xSemaphoreGive(s_mutex);
        return false;
    }
    s_robot.state = ROBOT_STATE_IDLE;
    s_robot.error_code = 0;
    s_robot.direction = DIR_STOP;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Error cleared → IDLE");
    return true;
}

robot_snapshot_t robot_state_snapshot(void) {
    robot_snapshot_t snap;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snap.state           = s_robot.state;
    snap.direction       = s_robot.direction;
    snap.expression      = s_robot.expression;
    snap.profile         = s_robot.profile;
    snap.arms            = s_robot.arms;
    snap.speed           = s_robot.speed;
    snap.mouth_brightness = s_robot.mouth_brightness;
    snap.power_on        = s_robot.power_on;
    snap.boot_time_us    = s_robot.boot_time_us;
    snap.error_code      = s_robot.error_code;
    xSemaphoreGive(s_mutex);
    return snap;
}

// =====================================================================
//  SETTERS (called by decision engine only)
// =====================================================================

void robot_set_direction(robot_dir_t dir) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.direction = dir;
    xSemaphoreGive(s_mutex);
}

void robot_set_speed(int speed) {
    if (speed < 0) speed = 0;
    if (speed > 100) speed = 100;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.speed = speed;
    xSemaphoreGive(s_mutex);
}

void robot_set_expression(robot_expression_t exp) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.expression = exp;
    xSemaphoreGive(s_mutex);
}

void robot_set_profile(motion_profile_t profile) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.profile = profile;
    xSemaphoreGive(s_mutex);
}

void robot_set_arms(robot_arms_t arms) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.arms = arms;
    xSemaphoreGive(s_mutex);
}

void robot_set_power(bool on) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.power_on = on;
    if (!on) s_robot.direction = DIR_STOP;
    xSemaphoreGive(s_mutex);
}

void robot_set_brightness(int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 15) brightness = 15;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.mouth_brightness = brightness;
    xSemaphoreGive(s_mutex);
}

void robot_set_custom_mouth(const uint8_t data[16]) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(s_robot.custom_mouth, data, 16);
    s_robot.expression = EXP_CUSTOM;
    xSemaphoreGive(s_mutex);
}

void robot_set_force_blink(bool blink) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_robot.force_blink = blink;
    xSemaphoreGive(s_mutex);
}

// =====================================================================
//  GETTERS (called by motion/display tasks)
// =====================================================================

robot_dir_t robot_get_direction(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    robot_dir_t d = s_robot.direction;
    xSemaphoreGive(s_mutex);
    return d;
}

int robot_get_speed(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int s = s_robot.speed;
    xSemaphoreGive(s_mutex);
    return s;
}

robot_expression_t robot_get_expression(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    robot_expression_t e = s_robot.expression;
    xSemaphoreGive(s_mutex);
    return e;
}

motion_profile_t robot_get_profile(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    motion_profile_t p = s_robot.profile;
    xSemaphoreGive(s_mutex);
    return p;
}

robot_arms_t robot_get_arms(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    robot_arms_t a = s_robot.arms;
    xSemaphoreGive(s_mutex);
    return a;
}

bool robot_get_power(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool p = s_robot.power_on;
    xSemaphoreGive(s_mutex);
    return p;
}

int robot_get_brightness(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int b = s_robot.mouth_brightness;
    xSemaphoreGive(s_mutex);
    return b;
}

void robot_get_custom_mouth(uint8_t out[16]) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(out, s_robot.custom_mouth, 16);
    xSemaphoreGive(s_mutex);
}

bool robot_get_and_clear_blink(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool b = s_robot.force_blink;
    s_robot.force_blink = false;
    xSemaphoreGive(s_mutex);
    return b;
}

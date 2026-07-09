#include "motion_ctrl.h"
#include "board/board_config.h"
#include "state/robot_state.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include <stdlib.h>
#include "driver/gpio.h"

static const char *TAG = "MOTION";

#define SERVO_STEP_DEG 4

static const int RAMP_STEPS[] = {
    [PROFILE_SMOOTH] = 2,
    [PROFILE_NORMAL] = 6,
    [PROFILE_AGGRESSIVE] = 25,
};

static const ledc_channel_t SERVO_LEFT_CHANNEL = LEDC_CHANNEL_0;
static const ledc_channel_t SERVO_RIGHT_CHANNEL = LEDC_CHANNEL_1;

static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static uint32_t servo_angle_to_duty(int angle) {
    angle = clamp_int(angle, 0, 180);
    uint32_t usec = 500 + (angle * 2000 / 180);
    return (usec * ((1U << 16) - 1U)) / 20000U;
}

static void setup_servos(void) {
    gpio_reset_pin(CURIE_LEFT_SHOULDER_GPIO);
    gpio_reset_pin(CURIE_RIGHT_SHOULDER_GPIO);

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t left = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = SERVO_LEFT_CHANNEL,
        .timer_sel = LEDC_TIMER_1,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = CURIE_LEFT_SHOULDER_GPIO,
        .duty = servo_angle_to_duty(90),
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&left));

    ledc_channel_config_t right = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = SERVO_RIGHT_CHANNEL,
        .timer_sel = LEDC_TIMER_1,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = CURIE_RIGHT_SHOULDER_GPIO,
        .duty = servo_angle_to_duty(90),
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&right));
}

static void set_servo_angle(ledc_channel_t channel, int angle) {
    uint32_t duty = servo_angle_to_duty(angle);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_HIGH_SPEED_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_HIGH_SPEED_MODE, channel));
}

static void init_motors(void) {
    uint8_t pins[] = {
        CURIE_M1_RPWM_GPIO, CURIE_M1_LPWM_GPIO,
        CURIE_M2_RPWM_GPIO, CURIE_M2_LPWM_GPIO,
        CURIE_M3_RPWM_GPIO, CURIE_M3_LPWM_GPIO,
        CURIE_M4_RPWM_GPIO, CURIE_M4_LPWM_GPIO
    };
    ledc_timer_config_t lt = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&lt);

    for (int i = 0; i < 8; i++) {
        ledc_channel_config_t ch = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0 + i,
            .timer_sel = LEDC_TIMER_0,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = pins[i],
            .duty = 0,
            .hpoint = 0
        };
        ledc_channel_config(&ch);
    }
}

static void set_motor_channels(int m1_r, int m1_l, int m2_r, int m2_l,
                               int m3_r, int m3_l, int m4_r, int m4_l) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, m1_r);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, m1_l);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, m2_r);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, m2_l);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, m3_r);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, m3_l);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6, m4_r);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, m4_l);

    for (int i = 0; i < 8; i++) {
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0 + i);
    }
}

static void encode_motor_pct(int pct, bool invert, int *rpwm, int *lpwm) {
    pct = clamp_int(pct, -100, 100);
    if (invert) {
        pct = -pct;
    }
    int pwm = (abs(pct) * 255) / 100;
    *rpwm = pct > 0 ? pwm : 0;
    *lpwm = pct < 0 ? pwm : 0;
}

static void set_motor_pct(int front_left_pct, int front_right_pct, int rear_left_pct, int rear_right_pct) {
    int m1_r = 0, m1_l = 0, m2_r = 0, m2_l = 0;
    int m3_r = 0, m3_l = 0, m4_r = 0, m4_l = 0;

    // The left-side drivetrain is mounted electrically reversed relative to the right side.
    encode_motor_pct(front_left_pct, true, &m1_r, &m1_l);
    encode_motor_pct(front_right_pct, false, &m2_r, &m2_l);
    encode_motor_pct(rear_left_pct, true, &m3_r, &m3_l);
    encode_motor_pct(rear_right_pct, false, &m4_r, &m4_l);

    set_motor_channels(m1_r, m1_l, m2_r, m2_l, m3_r, m3_l, m4_r, m4_l);
}

static void apply_drive_pattern(robot_drive_t drive) {
    int left_pct = clamp_int(drive.left, -100, 100);
    int right_pct = clamp_int(drive.right, -100, 100);

    set_motor_pct(left_pct, right_pct, left_pct, right_pct);
}

static int step_toward(int current, int target, int step) {
    if (current < target) return current + ((target - current < step) ? target - current : step);
    if (current > target) return current - ((current - target < step) ? current - target : step);
    return current;
}

static robot_drive_t drive_from_direction(robot_dir_t dir, int speed) {
    int h = speed / 2;
    int turn_speed = (speed * CURIE_TANK_TURN_SCALE_PCT) / 100;
    turn_speed = clamp_int(turn_speed, 30, 100);
    switch (dir) {
        case DIR_FWD:       return (robot_drive_t){ speed,  speed};
        case DIR_BWD:       return (robot_drive_t){-speed, -speed};
        // Tank turn: left wheels backward, right wheels forward for left turn
        case DIR_LEFT:      return (robot_drive_t){-turn_speed,  turn_speed};
        case DIR_RIGHT:     return (robot_drive_t){ turn_speed, -turn_speed};
        case DIR_FWD_LEFT:  return (robot_drive_t){ h,      speed};
        case DIR_FWD_RIGHT: return (robot_drive_t){ speed,  h};
        case DIR_BWD_LEFT:  return (robot_drive_t){-h,     -speed};
        case DIR_BWD_RIGHT: return (robot_drive_t){-speed, -h};
        case DIR_STOP:
        default:            return (robot_drive_t){0, 0};
    }
}

void task_motion(void *arg) {
    ESP_LOGI(TAG, "Initializing motion system for %s", CURIE_BOARD_NAME);

    setup_servos();

    int cur_ls = 90;
    int cur_rs = 90;
    set_servo_angle(SERVO_LEFT_CHANNEL, cur_ls);
    set_servo_angle(SERVO_RIGHT_CHANNEL, cur_rs);

    init_motors();

    int actual_left = 0;
    int actual_right = 0;

    ESP_LOGI(TAG, "Motion task running");

    while (1) {
        bool powered = robot_get_power();
        robot_state_t state = robot_state_get();
        robot_dir_t dir = robot_get_direction();
        int speed = robot_get_speed();
        motion_profile_t profile = robot_get_profile();
        robot_arms_t arms = robot_get_arms();
        bool direct_drive = robot_get_direct_drive();
        robot_drive_t target = direct_drive ? robot_get_drive() : drive_from_direction(dir, speed);

        if (!powered || state == ROBOT_STATE_ERROR) {
            target = (robot_drive_t){0, 0};
            actual_left = 0;
            actual_right = 0;
            set_motor_pct(0, 0, 0, 0);
        }

        if (powered && state != ROBOT_STATE_ERROR) {
            int new_ls = step_toward(cur_ls, arms.ls, SERVO_STEP_DEG);
            int new_rs = step_toward(cur_rs, arms.rs, SERVO_STEP_DEG);

            if (new_ls != cur_ls) {
                set_servo_angle(SERVO_LEFT_CHANNEL, new_ls);
                cur_ls = new_ls;
            }
            if (new_rs != cur_rs) {
                set_servo_angle(SERVO_RIGHT_CHANNEL, new_rs);
                cur_rs = new_rs;
            }
        }

        int profile_idx = clamp_int((int)profile, PROFILE_SMOOTH, PROFILE_AGGRESSIVE);
        int ramp = RAMP_STEPS[profile_idx];
        actual_left = step_toward(actual_left, target.left, ramp);
        actual_right = step_toward(actual_right, target.right, ramp);
        apply_drive_pattern((robot_drive_t){actual_left, actual_right});

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#include "motion_ctrl.h"
#include "board/board_config.h"
#include "state/robot_state.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/mcpwm_prelude.h"
#include <stdlib.h>
#include "driver/gpio.h"

static const char *TAG = "MOTION";

#define SERVO_STEP_DEG 4

static const int RAMP_STEPS[] = {
    [PROFILE_SMOOTH] = 2,
    [PROFILE_NORMAL] = 6,
    [PROFILE_AGGRESSIVE] = 25,
};

static mcpwm_cmpr_handle_t cmp_ls, cmp_rs;

static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static mcpwm_cmpr_handle_t setup_servo(int pin, int group_id) {
    gpio_reset_pin(pin);
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_timer_config_t tcfg = {
        .group_id = group_id,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,
        .period_ticks = 20000,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&tcfg, &timer));

    mcpwm_oper_handle_t oper = NULL;
    mcpwm_operator_config_t ocfg = {.group_id = group_id};
    ESP_ERROR_CHECK(mcpwm_new_operator(&ocfg, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    mcpwm_cmpr_handle_t comp = NULL;
    mcpwm_comparator_config_t ccfg = {.flags.update_cmp_on_tez = true};
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &ccfg, &comp));

    mcpwm_gen_handle_t gen = NULL;
    mcpwm_generator_config_t gcfg = {.gen_gpio_num = pin};
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &gcfg, &gen));

    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comp, 1500));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comp, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    return comp;
}

static void set_servo_angle(mcpwm_cmpr_handle_t comp, int angle) {
    angle = clamp_int(angle, 0, 180);
    uint32_t usec = 500 + (angle * 2000 / 180);
    mcpwm_comparator_set_compare_value(comp, usec);
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

static void set_tank_sides(int left_pct, int right_pct) {
    left_pct = clamp_int(left_pct, -100, 100);
    right_pct = clamp_int(right_pct, -100, 100);

    int left_pwm = (abs(left_pct) * 255) / 100;
    int right_pwm = (abs(right_pct) * 255) / 100;

    int left_fwd = left_pct > 0 ? left_pwm : 0;
    int left_bwd = left_pct < 0 ? left_pwm : 0;
    int right_fwd = right_pct > 0 ? right_pwm : 0;
    int right_bwd = right_pct < 0 ? right_pwm : 0;

    // Physical layout:
    //   M1 = front-left, M3 = rear-left
    //   M2 = front-right, M4 = rear-right
    // Tank grouping keeps M1+M3 on the left side and M2+M4 on the right side.
    set_motor_channels(left_fwd, left_bwd, right_fwd, right_bwd,
                       left_fwd, left_bwd, right_fwd, right_bwd);
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

    cmp_ls = setup_servo(CURIE_LEFT_SHOULDER_GPIO, 0);
    cmp_rs = setup_servo(CURIE_RIGHT_SHOULDER_GPIO, 1);

    int cur_ls = 90;
    int cur_rs = 90;
    set_servo_angle(cmp_ls, cur_ls);
    set_servo_angle(cmp_rs, cur_rs);

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
            set_tank_sides(0, 0);
        }

        if (powered && state != ROBOT_STATE_ERROR) {
            int new_ls = step_toward(cur_ls, arms.ls, SERVO_STEP_DEG);
            int new_rs = step_toward(cur_rs, arms.rs, SERVO_STEP_DEG);

            if (new_ls != cur_ls) {
                set_servo_angle(cmp_ls, new_ls);
                cur_ls = new_ls;
            }
            if (new_rs != cur_rs) {
                set_servo_angle(cmp_rs, new_rs);
                cur_rs = new_rs;
            }
        }

        int ramp = RAMP_STEPS[profile];
        actual_left = step_toward(actual_left, target.left, ramp);
        actual_right = step_toward(actual_right, target.right, ramp);
        set_tank_sides(actual_left, actual_right);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#include "motion_ctrl.h"
#include "state/robot_state.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/mcpwm_prelude.h"

static const char *TAG = "MOTION";

// =====================================================================
//  PIN DEFINITIONS (V2 - 4 Independent Drivers)
// =====================================================================

#define M1_RPWM 26
#define M1_LPWM 25
#define M2_RPWM 16
#define M2_LPWM 4
#define M3_RPWM 19
#define M3_LPWM 17
#define M4_RPWM 18
#define M4_LPWM 23

#define LEFT_SHOULDER_PIN  13
#define LEFT_ELBOW_PIN     12
#define RIGHT_SHOULDER_PIN 14
#define RIGHT_ELBOW_PIN    27

#define SERVO_STEP_DEG 4

// Ramp steps per profile
static const int RAMP_STEPS[] = {
    [PROFILE_SMOOTH]     = 4,
    [PROFILE_NORMAL]     = 8,
    [PROFILE_AGGRESSIVE] = 20,
};

// =====================================================================
//  SERVO SETUP (from V1)
// =====================================================================

static mcpwm_cmpr_handle_t cmp_ls, cmp_le, cmp_rs, cmp_re;

static mcpwm_cmpr_handle_t setup_servo(int pin, int group_id) {
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_timer_config_t tcfg = {
        .group_id = group_id, .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, .period_ticks = 20000, .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
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

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comp, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    return comp;
}

static void set_servo_angle(mcpwm_cmpr_handle_t comp, int angle) {
    uint32_t usec = 500 + (angle * 2000 / 180);
    mcpwm_comparator_set_compare_value(comp, usec);
}

// =====================================================================
//  MOTOR SETUP (V2 - 4 Independent Drivers)
// =====================================================================

static void init_motors(void) {
    uint8_t pins[] = {M1_RPWM, M1_LPWM, M2_RPWM, M2_LPWM, M3_RPWM, M3_LPWM, M4_RPWM, M4_LPWM};
    ledc_timer_config_t lt = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&lt);

    for (int i = 0; i < 8; i++) {
        ledc_channel_config_t ch = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0 + i,
            .timer_sel = LEDC_TIMER_0, .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = pins[i], .duty = 0, .hpoint = 0
        };
        ledc_channel_config(&ch);
    }
}

static void set_motor_speeds(int m1l, int m1r, int m2l, int m2r, int m3l, int m3r, int m4l, int m4r) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, m1l);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, m1r);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, m2l);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, m2r);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, m3l);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, m3r);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6, m4l);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, m4r);
    for (int i = 0; i < 8; i++) ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0 + i);
}

// =====================================================================
//  HELPERS
// =====================================================================

static int step_toward(int current, int target, int step) {
    if (current < target) return current + ((target - current < step) ? target - current : step);
    if (current > target) return current - ((current - target < step) ? current - target : step);
    return current;
}

// =====================================================================
//  TASK
// =====================================================================

void task_motion(void *arg) {
    ESP_LOGI(TAG, "Initializing Curie V2 Motion System (4-Driver Mode)...");

    // Init servos
    cmp_ls = setup_servo(LEFT_SHOULDER_PIN, 0);
    cmp_le = setup_servo(LEFT_ELBOW_PIN, 0);
    cmp_rs = setup_servo(RIGHT_SHOULDER_PIN, 0);
    cmp_re = setup_servo(RIGHT_ELBOW_PIN, 1);

    int cur_ls = 90, cur_le = 45, cur_rs = 90, cur_re = 45;
    set_servo_angle(cmp_ls, cur_ls);
    set_servo_angle(cmp_rs, cur_rs);
    set_servo_angle(cmp_le, cur_le);
    set_servo_angle(cmp_re, cur_re);

    // Init motors
    init_motors();

    int actual_speed = 0;
    robot_dir_t last_dir = DIR_STOP;

    ESP_LOGI(TAG, "Motion task running");

    while (1) {
        // Read current desired state
        bool powered = robot_get_power();
        robot_state_t state = robot_state_get();
        robot_dir_t dir = robot_get_direction();
        int tgt_speed = robot_get_speed();
        motion_profile_t profile = robot_get_profile();
        robot_arms_t arms = robot_get_arms();

        // Force stop in ERROR or power-off
        if (!powered || state == ROBOT_STATE_ERROR) {
            dir = DIR_STOP;
            tgt_speed = 0;
            actual_speed = 0;
            set_motor_speeds(0,0, 0,0, 0,0, 0,0); // Always force-zero
        }

        // Servo easing
        if (powered && state != ROBOT_STATE_ERROR) {
            int new_ls = step_toward(cur_ls, arms.ls, SERVO_STEP_DEG);
            int new_le = step_toward(cur_le, arms.le, SERVO_STEP_DEG);
            int new_rs = step_toward(cur_rs, arms.rs, SERVO_STEP_DEG);
            int new_re = step_toward(cur_re, arms.re, SERVO_STEP_DEG);

            if (new_ls != cur_ls) { set_servo_angle(cmp_ls, new_ls); cur_ls = new_ls; }
            if (new_le != cur_le) { set_servo_angle(cmp_le, new_le); cur_le = new_le; }
            if (new_rs != cur_rs) { set_servo_angle(cmp_rs, new_rs); cur_rs = new_rs; }
            if (new_re != cur_re) { set_servo_angle(cmp_re, new_re); cur_re = new_re; }
        }

        // Acceleration ramp
        int desired = (dir == DIR_STOP) ? 0 : tgt_speed;
        int ramp = RAMP_STEPS[profile];
        actual_speed = step_toward(actual_speed, desired, ramp);

        // Apply motor directions
        if (dir != last_dir || actual_speed > 0 || last_dir != DIR_STOP) {
            int s = (actual_speed * 255) / 100;
            int h = s / 2;

            switch (dir) {
                case DIR_FWD:       set_motor_speeds(s,0, s,0, s,0, s,0); break;
                case DIR_BWD:       set_motor_speeds(0,s, 0,s, 0,s, 0,s); break;
                case DIR_LEFT:      set_motor_speeds(0,s, s,0, 0,s, s,0); break;  // Tank turn left
                case DIR_RIGHT:     set_motor_speeds(s,0, 0,s, s,0, 0,s); break;  // Tank turn right
                case DIR_FWD_LEFT:  set_motor_speeds(h,0, s,0, h,0, s,0); break;
                case DIR_FWD_RIGHT: set_motor_speeds(s,0, h,0, s,0, h,0); break;
                case DIR_BWD_LEFT:  set_motor_speeds(0,h, 0,s, 0,h, 0,s); break;
                case DIR_BWD_RIGHT: set_motor_speeds(0,s, 0,h, 0,s, 0,h); break;
                case DIR_STOP:
                default:            set_motor_speeds(0,0, 0,0, 0,0, 0,0); break;
            }
            last_dir = dir;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

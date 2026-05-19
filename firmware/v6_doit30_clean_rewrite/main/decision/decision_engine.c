#include "decision_engine.h"
#include "events/event_bus.h"
#include "state/robot_state.h"
#include "logging/ring_log.h"
#include "demo/demo_mode.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DECISION";

#define CONTROLLER_PRIORITY_US 800000
#define ESTOP_ERROR_CODE 0xE5709001

static int64_t s_controller_priority_until_us = 0;

static bool is_user_source(event_source_t src) {
    return src == EVT_SRC_WEBSOCKET || src == EVT_SRC_HTTP || src == EVT_SRC_ESPNOW;
}

static bool is_motion_control_event(event_type_t type) {
    return type == EVT_MOVE ||
           type == EVT_SET_DRIVE ||
           type == EVT_SET_SPEED ||
           type == EVT_STOP ||
           type == EVT_SET_ARMS;
}

static bool controller_has_priority(int64_t now_us) {
    return now_us < s_controller_priority_until_us;
}

static void auto_enter_manual(event_source_t src) {
    if (!is_user_source(src)) return;

    robot_state_t cur = robot_state_get();
    if (cur == ROBOT_STATE_DEMO && src == EVT_SRC_ESPNOW) {
        demo_mode_stop();
        robot_state_transition(ROBOT_STATE_IDLE);
        cur = robot_state_get();
    }

    if (cur == ROBOT_STATE_IDLE) {
        robot_state_transition(ROBOT_STATE_MANUAL);
    }
}

static void auto_return_idle(void) {
    robot_state_t cur = robot_state_get();
    if (cur == ROBOT_STATE_MANUAL) {
        robot_drive_t drive = robot_get_drive();
        robot_dir_t dir = robot_get_direction();
        bool stopped = robot_get_direct_drive()
            ? (drive.left == 0 && drive.right == 0)
            : (dir == DIR_STOP);

        if (stopped && event_bus_pending() == 0) {
            robot_state_transition(ROBOT_STATE_IDLE);
        }
    }
}

static bool is_allowed(event_type_t type) {
    robot_state_t state = robot_state_get();

    if (state == ROBOT_STATE_ERROR) {
        return type == EVT_CLEAR_ERROR ||
               type == EVT_LOG_REQUEST ||
               type == EVT_HEARTBEAT ||
               type == EVT_ESTOP;
    }

    if (state == ROBOT_STATE_LOW_POWER) {
        return type != EVT_MOVE && type != EVT_SET_DRIVE &&
               type != EVT_SET_SPEED && type != EVT_SET_ARMS;
    }

    if (state == ROBOT_STATE_DEMO) {
        return type == EVT_STOP ||
               type == EVT_ENABLE_DEMO ||
               type == EVT_CHANGE_STATE ||
               type == EVT_SET_POWER ||
               type == EVT_HEARTBEAT ||
               type == EVT_LOG_REQUEST ||
               type == EVT_ESTOP;
    }

    return true;
}

void task_decision(void *arg) {
    ESP_LOGI(TAG, "Decision engine started");
    robot_event_t evt;

    while (1) {
        if (!event_bus_receive(&evt, 100)) {
            auto_return_idle();
            continue;
        }

        int64_t now_us = esp_timer_get_time();
        if (evt.source == EVT_SRC_ESPNOW) {
            s_controller_priority_until_us = now_us + CONTROLLER_PRIORITY_US;
        } else if (is_motion_control_event(evt.type) && controller_has_priority(now_us)) {
            ring_log_write("BLOCKED: dashboard control while controller active");
            continue;
        }

        ring_log_write("EVT: %s src=%d", event_type_name(evt.type), evt.source);

        if (!is_allowed(evt.type)) {
            ring_log_write("BLOCKED: %s in state %d", event_type_name(evt.type), robot_state_get());
            ESP_LOGW(TAG, "Blocked %s in state %d", event_type_name(evt.type), robot_state_get());
            continue;
        }

        switch (evt.type) {
            case EVT_MOVE:
                auto_enter_manual(evt.source);
                robot_set_direction(evt.payload.direction);
                break;

            case EVT_SET_DRIVE:
                auto_enter_manual(evt.source);
                robot_set_direct_drive(evt.payload.drive);
                break;

            case EVT_STOP:
                robot_set_direction(DIR_STOP);
                break;

            case EVT_SET_SPEED:
                auto_enter_manual(evt.source);
                robot_set_speed(evt.payload.int_val);
                break;

            case EVT_SET_EXPRESSION:
                auto_enter_manual(evt.source);
                robot_set_expression(evt.payload.expression);
                break;

            case EVT_SET_BRIGHTNESS:
                robot_set_brightness(evt.payload.int_val);
                break;

            case EVT_SET_CUSTOM_MOUTH:
                auto_enter_manual(evt.source);
                robot_set_custom_mouth(evt.payload.mouth_data);
                break;

            case EVT_BLINK:
                robot_set_force_blink(true);
                break;

            case EVT_SET_ARMS:
                auto_enter_manual(evt.source);
                robot_set_arms(evt.payload.arms);
                break;

            case EVT_SET_POWER:
                robot_set_power(evt.payload.bool_val);
                if (!evt.payload.bool_val) {
                    robot_set_direction(DIR_STOP);
                }
                ring_log_write("Power: %s", evt.payload.bool_val ? "ON" : "OFF");
                break;

            case EVT_SET_PROFILE:
                robot_set_profile(evt.payload.profile);
                ring_log_write("Profile: %d", evt.payload.profile);
                break;

            case EVT_CHANGE_STATE:
                if (!robot_state_transition(evt.payload.target_state)) {
                    ring_log_write("State transition rejected -> %d", evt.payload.target_state);
                }
                break;

            case EVT_ENABLE_DEMO: {
                robot_state_t cur = robot_state_get();
                if (cur == ROBOT_STATE_DEMO) {
                    demo_mode_stop();
                    robot_state_transition(ROBOT_STATE_IDLE);
                } else if (cur == ROBOT_STATE_IDLE) {
                    if (robot_state_transition(ROBOT_STATE_DEMO)) {
                        demo_mode_start();
                    }
                }
                break;
            }

            case EVT_ESTOP:
                robot_state_set_error(ESTOP_ERROR_CODE);
                ring_log_write("E-STOP latched");
                break;

            case EVT_CLEAR_ERROR:
                robot_state_clear_error();
                ring_log_write("Error cleared");
                break;

            case EVT_HEARTBEAT:
                break;

            case EVT_LOG_REQUEST:
                break;

            default:
                ESP_LOGW(TAG, "Unhandled event: %s", event_type_name(evt.type));
                break;
        }
    }
}

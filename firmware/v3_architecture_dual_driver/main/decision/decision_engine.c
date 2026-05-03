#include "decision_engine.h"
#include "events/event_bus.h"
#include "state/robot_state.h"
#include "logging/ring_log.h"
#include "demo/demo_mode.h"
#include "esp_log.h"

static const char *TAG = "DECISION";

// Auto-transition to MANUAL when a user command arrives while IDLE
static void auto_enter_manual(event_source_t src) {
    if (src == EVT_SRC_WEBSOCKET || src == EVT_SRC_HTTP) {
        robot_state_t cur = robot_state_get();
        if (cur == ROBOT_STATE_IDLE) {
            robot_state_transition(ROBOT_STATE_MANUAL);
        }
    }
}

// Auto-return to IDLE when the robot stops and no more commands are pending
static void auto_return_idle(void) {
    robot_state_t cur = robot_state_get();
    if (cur == ROBOT_STATE_MANUAL) {
        robot_dir_t dir = robot_get_direction();
        if (dir == DIR_STOP && event_bus_pending() == 0) {
            robot_state_transition(ROBOT_STATE_IDLE);
        }
    }
}

// Can this event be processed in the current state?
static bool is_allowed(event_type_t type) {
    robot_state_t state = robot_state_get();

    // ERROR blocks everything except CLEAR_ERROR and LOG_REQUEST
    if (state == ROBOT_STATE_ERROR) {
        return (type == EVT_CLEAR_ERROR || type == EVT_LOG_REQUEST || type == EVT_HEARTBEAT);
    }

    // LOW_POWER blocks motion but allows expressions and system events
    if (state == ROBOT_STATE_LOW_POWER) {
        return (type != EVT_MOVE && type != EVT_SET_SPEED && type != EVT_SET_ARMS);
    }

    // DEMO blocks user motion commands (demo script drives motion)
    if (state == ROBOT_STATE_DEMO) {
        return (type == EVT_STOP || type == EVT_ENABLE_DEMO || type == EVT_CHANGE_STATE ||
                type == EVT_SET_POWER || type == EVT_HEARTBEAT || type == EVT_LOG_REQUEST);
    }

    return true;
}

void task_decision(void *arg) {
    ESP_LOGI(TAG, "Decision engine started");
    robot_event_t evt;

    while (1) {
        // Block for up to 100ms waiting for an event
        if (!event_bus_receive(&evt, 100)) {
            // No event — run idle behavior
            auto_return_idle();

            // Behavior-linked expressions: idle → blinking neutral
            robot_state_t s = robot_state_get();
            if (s == ROBOT_STATE_IDLE && robot_get_power()) {
                // Idle state is handled by display task's blink timer
            }
            continue;
        }

        // Log all events
        ring_log_write("EVT: %s src=%d", event_type_name(evt.type), evt.source);

        // Gate check
        if (!is_allowed(evt.type)) {
            ring_log_write("BLOCKED: %s in state %d", event_type_name(evt.type), robot_state_get());
            ESP_LOGW(TAG, "Blocked %s in state %d", event_type_name(evt.type), robot_state_get());
            continue;
        }

        // Process the event
        switch (evt.type) {
            case EVT_MOVE:
                auto_enter_manual(evt.source);
                robot_set_direction(evt.payload.direction);

                // Behavior-linked: motion → focused face (only if no manual override)
                if (evt.payload.direction != DIR_STOP) {
                    robot_state_t s = robot_state_get();
                    if (s == ROBOT_STATE_MANUAL || s == ROBOT_STATE_IDLE) {
                        // Don't override manual expression choices
                    }
                }
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
                    ring_log_write("State transition REJECTED → %d", evt.payload.target_state);
                }
                break;

            case EVT_ENABLE_DEMO: {
                robot_state_t cur = robot_state_get();
                if (cur == ROBOT_STATE_DEMO) {
                    // Stop demo
                    demo_mode_stop();
                    robot_state_transition(ROBOT_STATE_IDLE);
                } else if (cur == ROBOT_STATE_IDLE) {
                    // Start demo
                    if (robot_state_transition(ROBOT_STATE_DEMO)) {
                        demo_mode_start();
                    }
                }
                break;
            }

            case EVT_CLEAR_ERROR:
                robot_state_clear_error();
                ring_log_write("Error cleared");
                break;

            case EVT_HEARTBEAT:
                // Keepalive — no action needed
                break;

            case EVT_LOG_REQUEST:
                // Handled by telemetry task reading the ring buffer
                break;

            default:
                ESP_LOGW(TAG, "Unhandled event: %s", event_type_name(evt.type));
                break;
        }
    }
}

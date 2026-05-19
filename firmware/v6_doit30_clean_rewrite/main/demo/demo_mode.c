#include "demo_mode.h"
#include "events/event_bus.h"
#include "state/robot_state.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "DEMO";

static esp_timer_handle_t s_demo_timer = NULL;
static int s_demo_step = 0;

// The sequence script
static void demo_timer_cb(void *arg) {
    // Only proceed if still in DEMO state
    if (robot_state_get() != ROBOT_STATE_DEMO) {
        demo_mode_stop();
        return;
    }

    event_payload_t p;
    switch (s_demo_step % 8) {
        case 0:
            p.expression = EXP_LOVE;
            event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_INTERNAL, p);
            p.arms = (robot_arms_t){180, 45, 0, 45}; // Mirrored shoulders up
            event_bus_send(EVT_SET_ARMS, EVT_SRC_INTERNAL, p);
            break;
        case 1:
            p.direction = DIR_LEFT;
            event_bus_send(EVT_MOVE, EVT_SRC_INTERNAL, p);
            break;
        case 2:
            p.direction = DIR_STOP;
            event_bus_send(EVT_MOVE, EVT_SRC_INTERNAL, p);
            p.expression = EXP_WINK;
            event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_INTERNAL, p);
            break;
        case 3:
            p.direction = DIR_RIGHT;
            event_bus_send(EVT_MOVE, EVT_SRC_INTERNAL, p);
            break;
        case 4:
            p.direction = DIR_STOP;
            event_bus_send(EVT_MOVE, EVT_SRC_INTERNAL, p);
            p.expression = EXP_SLEEP;
            event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_INTERNAL, p);
            p.arms = (robot_arms_t){45, 45, 135, 45}; // Mirrored shoulders down
            event_bus_send(EVT_SET_ARMS, EVT_SRC_INTERNAL, p);
            break;
        case 5:
            p.direction = DIR_FWD;
            event_bus_send(EVT_MOVE, EVT_SRC_INTERNAL, p);
            break;
        case 6:
            p.direction = DIR_BWD;
            event_bus_send(EVT_MOVE, EVT_SRC_INTERNAL, p);
            break;
        case 7:
            p.direction = DIR_STOP;
            event_bus_send(EVT_MOVE, EVT_SRC_INTERNAL, p);
            p.expression = EXP_HAPPY;
            event_bus_send(EVT_SET_EXPRESSION, EVT_SRC_INTERNAL, p);
            p.arms = (robot_arms_t){90, 45, 90, 45}; // Rest
            event_bus_send(EVT_SET_ARMS, EVT_SRC_INTERNAL, p);
            break;
    }
    s_demo_step++;
}

void demo_mode_start(void) {
    if (s_demo_timer) return; // already running

    esp_timer_create_args_t cfg = {
        .callback = &demo_timer_cb,
        .name = "demo_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&cfg, &s_demo_timer));
    
    s_demo_step = 0;
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_demo_timer, 2000000)); // Every 2s
    ESP_LOGI(TAG, "Demo mode started");
}

void demo_mode_stop(void) {
    if (s_demo_timer) {
        esp_timer_stop(s_demo_timer);
        esp_timer_delete(s_demo_timer);
        s_demo_timer = NULL;
        ESP_LOGI(TAG, "Demo mode stopped");
    }
    
    // Safety stop
    event_bus_send_simple(EVT_STOP, EVT_SRC_INTERNAL);
}

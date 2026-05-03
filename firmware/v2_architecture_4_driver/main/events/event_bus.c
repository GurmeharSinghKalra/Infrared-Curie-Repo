#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "EVT_BUS";

#define EVENT_QUEUE_DEPTH 32

static QueueHandle_t s_event_queue = NULL;

void event_bus_init(void) {
    s_event_queue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(robot_event_t));
    configASSERT(s_event_queue);
    ESP_LOGI(TAG, "Event bus initialized (depth=%d)", EVENT_QUEUE_DEPTH);
}

bool event_bus_post(const robot_event_t *event) {
    if (!s_event_queue) return false;
    if (xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue FULL, dropped: %s", event_type_name(event->type));
        return false;
    }
    return true;
}

bool event_bus_send(event_type_t type, event_source_t source, event_payload_t payload) {
    robot_event_t evt = {
        .type = type,
        .source = source,
        .timestamp_us = esp_timer_get_time(),
        .payload = payload
    };
    return event_bus_post(&evt);
}

bool event_bus_send_simple(event_type_t type, event_source_t source) {
    event_payload_t empty = {0};
    return event_bus_send(type, source, empty);
}

bool event_bus_receive(robot_event_t *out, uint32_t timeout_ms) {
    if (!s_event_queue) return false;
    return xQueueReceive(s_event_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

int event_bus_pending(void) {
    if (!s_event_queue) return 0;
    return (int)uxQueueMessagesWaiting(s_event_queue);
}

const char *event_type_name(event_type_t type) {
    static const char *names[] = {
        [EVT_MOVE]              = "MOVE",
        [EVT_SET_SPEED]         = "SET_SPEED",
        [EVT_STOP]              = "STOP",
        [EVT_SET_EXPRESSION]    = "SET_EXPRESSION",
        [EVT_SET_BRIGHTNESS]    = "SET_BRIGHTNESS",
        [EVT_SET_CUSTOM_MOUTH]  = "SET_CUSTOM_MOUTH",
        [EVT_BLINK]             = "BLINK",
        [EVT_SET_ARMS]          = "SET_ARMS",
        [EVT_SET_POWER]         = "SET_POWER",
        [EVT_CHANGE_STATE]      = "CHANGE_STATE",
        [EVT_SET_PROFILE]       = "SET_PROFILE",
        [EVT_ENABLE_DEMO]       = "ENABLE_DEMO",
        [EVT_CLEAR_ERROR]       = "CLEAR_ERROR",
        [EVT_HEARTBEAT]         = "HEARTBEAT",
        [EVT_LOG_REQUEST]       = "LOG_REQUEST",
    };
    if (type >= 0 && type < EVT_COUNT) return names[type];
    return "UNKNOWN";
}

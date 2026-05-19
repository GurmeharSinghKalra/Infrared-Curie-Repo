#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "state/robot_state.h"
#include "events/event_bus.h"
#include "logging/ring_log.h"
#include "decision/decision_engine.h"
#include "motion/motion_ctrl.h"
#include "display/display_ctrl.h"
#include "telemetry/telemetry.h"
#include "network/wifi_server.h"
#include "network/espnow_controller.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ring_log_init();
    robot_state_init();
    event_bus_init();

    start_wifi_server();
    ESP_LOGI(TAG, "ESP-NOW temporarily disabled until AP stability is confirmed");
    xTaskCreatePinnedToCore(task_telemetry, "task_telemetry", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(task_decision, "task_decision", 4096, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(task_motion, "task_motion", 8192, NULL, 8, NULL, 1);
    xTaskCreatePinnedToCore(task_display, "task_display", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Curie V5 clean rewrite boot complete");
}

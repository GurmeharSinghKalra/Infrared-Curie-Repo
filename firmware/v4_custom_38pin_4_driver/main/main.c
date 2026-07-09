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

void app_main(void) {
    // 1. Initialize core state and logging
    ring_log_init();
    robot_state_init();
    event_bus_init();

    // 2. Start Network and Telemetry (Core 0)
    start_wifi_server();
    ESP_LOGI("MAIN", "ESP-NOW disabled for SoftAP DHCP diagnostic build");
    xTaskCreatePinnedToCore(task_telemetry, "task_telemetry", 4096, NULL, 4, NULL, 0);

    // 3. Start Decision Engine (Core 0, high priority)
    xTaskCreatePinnedToCore(task_decision, "task_decision", 4096, NULL, 10, NULL, 0);

    // 4. Start Hardware Control Tasks (Core 1)
    // Motion needs high priority to ensure smooth ramping
    xTaskCreatePinnedToCore(task_motion, "task_motion", 8192, NULL, 8, NULL, 1);
    
    // Display handles I2C and SPI, keep priority medium
    xTaskCreatePinnedToCore(task_display, "task_display", 8192, NULL, 5, NULL, 1);

    ESP_LOGI("MAIN", "V4 Custom Controller Build Boot Complete");
}

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
#include "board/board_config.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

static void task_start_runtime_modules(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1500));

    xTaskCreatePinnedToCore(task_telemetry, "task_telemetry", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(task_decision, "task_decision", 4096, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(task_motion, "task_motion", 8192, NULL, 8, NULL, 1);
    xTaskCreatePinnedToCore(task_display, "task_display", 8192, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "Core runtime modules started");

    vTaskDelay(pdMS_TO_TICKS(3000));
    start_espnow_controller();
    ESP_LOGI(TAG, "ESP-NOW controller receiver enabled after AP/dashboard startup window");

    vTaskDelete(NULL);
}

void app_main(void) {
    ring_log_init();
    robot_state_init();
    event_bus_init();

    ESP_LOGI(TAG, "Booting firmware for %s", CURIE_BOARD_NAME);
    start_wifi_server();
    xTaskCreatePinnedToCore(task_start_runtime_modules, "task_runtime_boot", 4096, NULL, 6, NULL, 0);

    ESP_LOGI(TAG, "Curie DOIT DevKit V1 rewrite boot complete");
    ESP_LOGI(TAG, "AP/dashboard path starts first; ESP-NOW is enabled after the initial startup window");
}

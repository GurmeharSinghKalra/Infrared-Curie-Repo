idf_component_register(
    SRCS
        "main.c"
        "state/robot_state.c"
        "events/event_bus.c"
        "decision/decision_engine.c"
        "motion/motion_ctrl.c"
        "display/display_ctrl.c"
        "telemetry/telemetry.c"
        "logging/ring_log.c"
        "demo/demo_mode.c"
        "network/wifi_server.c"
        "drivers/max7219_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_wifi esp_http_server nvs_flash json lwip app_update u8g2
    EMBED_FILES "dashboard/index.html"
                "dashboard/style.css"
                "dashboard/app.js"
)

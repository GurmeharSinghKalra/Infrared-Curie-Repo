idf_component_register(SRCS "main.c" "keypad.c" "oled_ui.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash console driver esp_adc bt u8g2)

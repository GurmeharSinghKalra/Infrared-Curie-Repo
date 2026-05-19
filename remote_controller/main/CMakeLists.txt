idf_component_register(SRCS "main.c" "keypad.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash console driver esp_adc bt)

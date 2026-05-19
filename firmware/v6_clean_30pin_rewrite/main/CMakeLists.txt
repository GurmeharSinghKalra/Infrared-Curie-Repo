idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer nvs_flash esp_wifi esp_netif lwip esp_http_server cJSON esp_event mcpwm driver ledc
)

# We will attach our static HTML/JS assets to the main component
# target_add_binary_data(${COMPONENT_LIB} "../web/index.html" TEXT)
# target_add_binary_data(${COMPONENT_LIB} "../web/app.js" TEXT)
# target_add_binary_data(${COMPONENT_LIB} "../web/style.css" TEXT)

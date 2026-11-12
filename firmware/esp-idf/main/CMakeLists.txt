idf_component_register(
    SRCS "main.c" "cloud_client.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_http_client nvs_flash json
)

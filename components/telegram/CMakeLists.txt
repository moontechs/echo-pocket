idf_component_register(
    SRCS "telegram_client.c" "telegram_caption.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_http_client esp-tls json storage
)

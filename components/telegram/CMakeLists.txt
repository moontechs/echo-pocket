idf_component_register(
    SRCS "telegram_client.c" "telegram_caption.c" "telegram_error.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_http_client esp-tls mbedtls json storage
)

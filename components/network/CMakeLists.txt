idf_component_register(
    SRCS "net_selection.c" "wifi_manager.c" "upload_task.c" "upload_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_wifi esp_netif esp_event nvs_flash
              storage recorder board telegram mp3enc
)

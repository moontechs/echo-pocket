idf_component_register(
    SRCS "board_pins.c" "buttons.c" "battery.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio freertos esp_event esp_adc
)

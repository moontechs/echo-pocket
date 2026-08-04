idf_component_register(
    SRCS "display.c"
         "face_engine/face_registry.cpp"
    INCLUDE_DIRS "include"
                 "face_engine/include"
    REQUIRES esp_driver_spi esp_lcd board esp_log
)

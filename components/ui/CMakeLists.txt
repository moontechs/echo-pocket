idf_component_register(
    SRCS "display.c"
         "face_engine/face_registry.cpp"
         "face_engine/owl_face.cpp"
         "face_engine/minimal_face.cpp"
         "face_engine/robot_face.cpp"
         "face_engine/pixel_face.cpp"
    INCLUDE_DIRS "include"
                 "face_engine/include"
    REQUIRES esp_driver_spi esp_lcd board esp_log
)

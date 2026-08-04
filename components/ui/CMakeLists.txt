idf_component_register(
    SRCS "display.c"
         "ui_task.c"
         "ui_screen_logic.c"
         "screens/home_screen.c"
         "screens/recording_screen.c"
         "screens/menu.c"
         "screens/list_pagination.c"
         "screens/recordings_list.c"
         "screens/unsent_list.c"
         "face_engine/face_registry.cpp"
         "face_engine/owl_face.cpp"
         "face_engine/minimal_face.cpp"
         "face_engine/robot_face.cpp"
         "face_engine/pixel_face.cpp"
    INCLUDE_DIRS "include"
                 "face_engine/include"
    REQUIRES esp_driver_spi esp_lcd board log esp_event recorder audio esp_timer storage
)

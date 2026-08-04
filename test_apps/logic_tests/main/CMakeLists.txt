idf_component_register(
    SRCS "test_main.c" "test_buttons.c" "test_sd_storage.c" "test_config.c"
    INCLUDE_DIRS "."
    REQUIRES unity board storage
)

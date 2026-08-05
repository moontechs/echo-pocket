idf_component_register(
    SRCS "sd_storage.c" "config.c" "queue_store.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs driver board
)

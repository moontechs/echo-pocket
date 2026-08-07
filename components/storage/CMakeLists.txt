idf_component_register(
    SRCS "sd_storage.c" "sd_storage_pure.c" "config.c" "queue_store.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs driver board
)

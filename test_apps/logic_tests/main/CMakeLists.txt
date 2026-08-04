idf_component_register(
    SRCS "test_main.c" "test_buttons.c" "test_sd_storage.c" "test_config.c"
         "../../components/audio/audio_ringbuf.c"
         "../../components/audio/test/test_ring_buffer.c"
    INCLUDE_DIRS "."
                "../../components/audio/include"
    REQUIRES unity board storage
)

idf_component_register(
    SRCS "test_main.c" "test_buttons.c" "test_sd_storage.c" "test_config.c"
         "../../components/audio/audio_ringbuf.c"
         "../../components/audio/test/test_ring_buffer.c"
         "../../components/recorder/wav_writer.c"
         "../../components/recorder/rec_id.c"
         "../../components/recorder/recorder_logic.c"
         "../../components/recorder/test/test_wav_writer.c"
         "../../components/recorder/test/test_rec_id.c"
    INCLUDE_DIRS "."
                "../../components/audio/include"
                "../../components/recorder/include"
    REQUIRES unity board storage esp_log
)

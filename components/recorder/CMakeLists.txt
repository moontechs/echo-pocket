idf_component_register(
    SRCS "wav_writer.c" "rec_id.c" "recorder_logic.c" "recorder.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos storage esp_timer audio esp_event board
)

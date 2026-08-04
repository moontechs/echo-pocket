idf_component_register(
    SRCS "audio_ringbuf.c" "audio_capture.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board freertos esp_codec_dev
)

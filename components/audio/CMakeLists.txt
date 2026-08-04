idf_component_register(
    SRCS "audio_ringbuf.c" "audio_capture.c" "audio_mono_ringbuf.c" "audio_process.c" "voice_level.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board freertos esp_codec_dev storage
)

idf_component_register(
    SRCS "main.c" "audio.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2s
    EMBED_FILES "../low_sonic.wav"
)

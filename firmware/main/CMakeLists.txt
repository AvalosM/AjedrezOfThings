idf_component_register(
    SRCS "main.c" "display.c" "audio.c" "buttons.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES link esp_lcd esp_driver_spi esp_driver_i2s esp_driver_gpio esp_timer
)

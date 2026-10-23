# The organizers' drivers for the kiwi board, unchanged.
idf_component_register(
    SRCS "display.c" "buttons.c" "audio.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2s
)

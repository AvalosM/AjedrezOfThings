# Uses the organizers' drivers straight from firmware/main (unmodified).
idf_component_register(
    SRCS "link_poc.c" "../../main/display.c" "../../main/buttons.c"
    INCLUDE_DIRS "." "../../main"
    PRIV_REQUIRES link esp_lcd esp_driver_spi esp_driver_gpio esp_timer
)

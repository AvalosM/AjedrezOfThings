idf_component_register(
    SRCS "link.c" "presence.c" "radio.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer
)

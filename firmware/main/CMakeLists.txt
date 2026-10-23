idf_component_register(
    SRCS "main.c" "events.c" "screens.c" "player.c" "chess.c" "game.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES kiwi_board link nvs_flash
)

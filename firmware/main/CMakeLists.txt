idf_component_register(
    SRCS "main.c" "events.c" "screens.c" "player.c" "chessboard.c" "match.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES kiwi_board link nvs_flash
)

idf_component_register(
    SRCS "main.c" "events.c" "screens.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES kiwi_board link
)

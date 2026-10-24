#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLAYER_NAME_LEN 4

bool player_profile_load(char *name_out, size_t max_len);
bool load_player(char *name_out, size_t max_len);

void save_player_name(const char *name_in);

void player_profile_arcade_input(char *name_out);
bool player_welcome_screen(const char *name, uint32_t timeout_ms);

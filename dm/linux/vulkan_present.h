#pragma once
#include <SDL.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void dm_linux_vk_set_window(SDL_Window *window);
int dm_linux_vk_initialize(const char *shader_directory, char *error, size_t bytes);
int dm_linux_vk_has_window(void);
#ifdef __cplusplus
}
#endif

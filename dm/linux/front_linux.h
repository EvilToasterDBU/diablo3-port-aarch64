#pragma once
#include <SDL.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int front_linux_window_size(int *width, int *height);
void dm_linux_ui_init(void);
void dm_linux_ui_toggle(void);
int dm_linux_ui_is_open(void);
void dm_linux_ui_event(const SDL_Event *event, int window_width, int window_height);
void dm_linux_ui_text_open(const char *title);
void dm_linux_ui_text_close(void);
int dm_linux_ui_text_active(void);
int dm_linux_ui_text_event(const SDL_Event *event, int window_width, int window_height);
int dm_linux_ui_overlay(int screen_width, int screen_height, unsigned char *pixels, size_t capacity,
                        int bgra, int *x, int *y, int *width, int *height);
void front_linux_text_snapshot(char *out, size_t capacity);
void front_linux_text_backspace(void);
void front_linux_text_finish(int accepted);
#ifdef __cplusplus
}
#endif

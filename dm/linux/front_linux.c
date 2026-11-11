#include "../host/dm.h"
#include "../host/dm_input.h"
#include "../host/front.h"
#include "front_linux.h"
#include "vulkan_present.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Window *g_window;
static SDL_GameController *g_pad;
static _Atomic int g_width, g_height;
static _Atomic int g_guest_done;
static void (*g_guest)(void);

// The guest invokes the software keyboard from its own thread. SDL text input
// must be started and stopped on the window thread, so the guest posts a
// request here and waits while the normal event loop owns the keyboard state.
static pthread_mutex_t g_text_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_text_cond = PTHREAD_COND_INITIALIZER;
static int g_text_requested, g_text_active, g_text_done, g_text_accepted;
static int g_text_max_chars;
static size_t g_text_len;
static char g_text_buf[3006];
static char g_text_title[128];
static size_t utf8_sequence(const unsigned char *s, size_t left, unsigned *units) {
    unsigned char c = s[0];
    size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (n > left) n = 1;
    for (size_t i = 1; i < n; ++i) if ((s[i] & 0xc0) != 0x80) { n = 1; break; }
    *units = n == 4 ? 2 : 1; // Switch swkbd limit is counted in UTF-16 units.
    return n;
}

static unsigned utf8_units(const char *s, size_t n) {
    unsigned total = 0;
    for (size_t i = 0; i < n;) { unsigned units; size_t step = utf8_sequence((const unsigned char *)s + i, n - i, &units); total += units; i += step; }
    return total;
}

static void text_append_locked(const char *s) {
    size_t n = strlen(s), i = 0;
    unsigned used = utf8_units(g_text_buf, g_text_len);
    while (i < n) {
        unsigned units;
        size_t step = utf8_sequence((const unsigned char *)s + i, n - i, &units);
        if (g_text_max_chars > 0 && used + units > (unsigned)g_text_max_chars) break;
        if (g_text_len + step >= sizeof g_text_buf) break;
        memcpy(g_text_buf + g_text_len, s + i, step);
        g_text_len += step; g_text_buf[g_text_len] = 0; used += units; i += step;
    }
}

static void text_backspace_locked(void) {
    if (!g_text_len) return;
    size_t i = g_text_len - 1;
    while (i && (((unsigned char)g_text_buf[i] & 0xc0) == 0x80)) --i;
    g_text_len = i; g_text_buf[i] = 0;
}

static int text_begin_on_window_thread(void) {
    int begin = 0; char title[sizeof g_text_title] = "ENTER NAME";
    pthread_mutex_lock(&g_text_mutex);
    if (g_text_requested && !g_text_active && !g_text_done) {
        g_text_requested = 0; g_text_active = 1; begin = 1; snprintf(title,sizeof title,"%s",g_text_title);
    }
    pthread_mutex_unlock(&g_text_mutex);
    if (begin) {
        SDL_StartTextInput();
        dm_linux_ui_text_open(title);
        LOG("SDL text input started with in-window keyboard: %s",title);
    }
    return begin;
}

static size_t text_key_fallback(const SDL_Keysym *key, char out[5]) {
    SDL_Keycode sym = key->sym;
    SDL_Keymod mod = (SDL_Keymod)key->mod;
    int shift = (mod & KMOD_SHIFT) != 0, caps = (mod & KMOD_CAPS) != 0;
    if (sym >= SDLK_a && sym <= SDLK_z) {
        out[0] = (char)((shift != caps) ? sym - SDLK_a + 'A' : sym);
        return 1;
    }
    if (sym >= SDLK_0 && sym <= SDLK_9) {
        static const char shifted[] = ")!@#$%^&*(";
        out[0] = shift ? shifted[sym - SDLK_0] : (char)sym;
        return 1;
    }
    if (sym >= SDLK_KP_0 && sym <= SDLK_KP_9) { out[0] = (char)('0' + sym - SDLK_KP_0); return 1; }
    if (sym == SDLK_SPACE) { out[0] = ' '; return 1; }
    if (sym == SDLK_MINUS || sym == SDLK_EQUALS || sym == SDLK_LEFTBRACKET || sym == SDLK_RIGHTBRACKET ||
        sym == SDLK_BACKSLASH || sym == SDLK_SEMICOLON || sym == SDLK_QUOTE || sym == SDLK_COMMA ||
        sym == SDLK_PERIOD || sym == SDLK_SLASH || sym == SDLK_BACKQUOTE) {
        static const char plain[] = "-=[]\\;',./`";
        static const char shifted[] = "_+{}|:\"<>?~";
        switch (sym) {
            case SDLK_MINUS: out[0] = shift ? shifted[0] : plain[0]; break;
            case SDLK_EQUALS: out[0] = shift ? shifted[1] : plain[1]; break;
            case SDLK_LEFTBRACKET: out[0] = shift ? shifted[2] : plain[2]; break;
            case SDLK_RIGHTBRACKET: out[0] = shift ? shifted[3] : plain[3]; break;
            case SDLK_BACKSLASH: out[0] = shift ? shifted[4] : plain[4]; break;
            case SDLK_SEMICOLON: out[0] = shift ? shifted[5] : plain[5]; break;
            case SDLK_QUOTE: out[0] = shift ? shifted[6] : plain[6]; break;
            case SDLK_COMMA: out[0] = shift ? shifted[7] : plain[7]; break;
            case SDLK_PERIOD: out[0] = shift ? shifted[8] : plain[8]; break;
            case SDLK_SLASH: out[0] = shift ? shifted[9] : plain[9]; break;
            default: out[0] = shift ? shifted[10] : plain[10]; break;
        }
        return 1;
    }
    // Some X11 input methods encode a Unicode keysym directly.
    if (((uint32_t)sym & 0xff000000u) == 0x01000000u) {
        uint32_t cp = (uint32_t)sym & 0x00ffffffu;
        if (cp <= 0x7f) { out[0] = (char)cp; return 1; }
        if (cp <= 0x7ff) { out[0] = (char)(0xc0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3f)); return 2; }
        if (cp <= 0xffff && !(cp >= 0xd800 && cp <= 0xdfff)) { out[0] = (char)(0xe0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[2] = (char)(0x80 | (cp & 0x3f)); return 3; }
        if (cp <= 0x10ffff) { out[0] = (char)(0xf0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[3] = (char)(0x80 | (cp & 0x3f)); return 4; }
    }
    return 0;
}

static int text_event(const SDL_Event *event, char *fallback, size_t fallback_cap,
                      size_t *fallback_len, int *saw_text_event) {
    int finish = 0;
    pthread_mutex_lock(&g_text_mutex);
    if (g_text_active && !g_text_done) {
        if (event->type == SDL_TEXTINPUT) { *saw_text_event = 1; text_append_locked(event->text.text); }
        else if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
            switch (event->key.keysym.sym) {
                case SDLK_BACKSPACE: text_backspace_locked(); break;
                case SDLK_RETURN: case SDLK_KP_ENTER:
                    g_text_accepted = 1; g_text_done = 1; finish = 1; break;
                case SDLK_ESCAPE:
                    g_text_accepted = 0; g_text_done = 1; finish = 1; break;
                default: {
                    char key_text[5]; size_t n = text_key_fallback(&event->key.keysym, key_text);
                    if (n && *fallback_len + n < fallback_cap) { memcpy(fallback + *fallback_len, key_text, n); *fallback_len += n; }
                    break;
                }
            }
        }
        if (finish) { g_text_active = 0; pthread_cond_broadcast(&g_text_cond); }
    }
    pthread_mutex_unlock(&g_text_mutex);
    if (finish) { SDL_StopTextInput(); dm_linux_ui_text_close(); }
    return finish;
}

void front_linux_text_snapshot(char *out, size_t capacity) {
    if(!out||!capacity)return;
    pthread_mutex_lock(&g_text_mutex);snprintf(out,capacity,"%s",g_text_buf);pthread_mutex_unlock(&g_text_mutex);
}
void front_linux_text_backspace(void) {
    pthread_mutex_lock(&g_text_mutex);if(g_text_active&&!g_text_done)text_backspace_locked();pthread_mutex_unlock(&g_text_mutex);
}
void front_linux_text_finish(int accepted) {
    int finish=0;
    pthread_mutex_lock(&g_text_mutex);
    if(g_text_active&&!g_text_done){g_text_accepted=!!accepted;g_text_done=1;g_text_active=0;finish=1;pthread_cond_broadcast(&g_text_cond);}
    pthread_mutex_unlock(&g_text_mutex);
    if(finish){SDL_StopTextInput();dm_linux_ui_text_close();}
}

static unsigned render_height(void) {
    const char *value = getenv("DM_LINUX_RENDER_HEIGHT");
    if (!value || !*value) return 720;
    char *end = NULL; unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end || parsed < 360 || parsed > 1440) return 720;
    return (unsigned)parsed;
}

static void apply_left_stick_deadzone(int32_t *x, int32_t *y) {
    const char *setting = getenv("DM_LINUX_DEADZONE"); double dz = setting ? strtod(setting, NULL) / 100.0 : 0.05;
    if (dz < 0.0 || dz > 0.5) dz = 0.05;
    const double dead = 32767.0 * dz;
    double magnitude = sqrt((double)*x * *x + (double)*y * *y);
    if (magnitude <= dead) { *x = *y = 0; return; }
    double clipped = magnitude > 32767.0 ? 32767.0 : magnitude;
    double scaled = (clipped - dead) * 32767.0 / (32767.0 - dead);
    double factor = scaled / magnitude;
    *x = (int32_t)lrint(*x * factor);
    *y = (int32_t)lrint(*y * factor);
}

static void publish_controller(void) {
    uint64_t buttons = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    static int combo_was_down;
    SDL_GameController *pad = g_pad;
    if (pad) {
        const SDL_GameControllerButton map[] = {
            SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
            SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSTICK,
            SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID,
            SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_START,
            SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
            SDL_CONTROLLER_BUTTON_DPAD_DOWN
        };
        for (unsigned i = 0; i < sizeof map / sizeof map[0]; ++i)
            if (map[i] != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(pad, map[i])) buttons |= 1ull << i;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) buttons |= DM_BTN_ZL;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) buttons |= DM_BTN_ZR;
        lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        ly = -(int32_t)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
        ry = -(int32_t)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
        apply_left_stick_deadzone(&lx, &ly);
        int combo = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK) &&
                    SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
        if (combo && !combo_was_down) dm_linux_ui_toggle();
        combo_was_down = combo;
        if (combo) buttons &= ~((1ull << 4) | (1ull << 5));
    }
    if (dm_linux_ui_is_open() || dm_linux_ui_text_active()) buttons = 0, lx = ly = rx = ry = 0;
    dm_pad_set(buttons, lx, ly, rx, ry);
}

static void *guest_main(void *unused) {
    (void)unused;
    static unsigned char alt_stack[64 * 1024];
    stack_t stack = {.ss_sp = alt_stack, .ss_size = sizeof alt_stack};
    sigaltstack(&stack, NULL);
    g_guest();
    atomic_store(&g_guest_done, 1);
    return NULL;
}

int front_linux_window_size(int *w, int *h) {
    if (!w || !h) return 0;
    *w = atomic_load(&g_width); *h = atomic_load(&g_height);
    return *w > 0 && *h > 0;
}
double front_display_refresh(char *name, size_t n) {
    if (name && n) name[0] = 0;
    if (!g_window) return 0;
    int display = SDL_GetWindowDisplayIndex(g_window);
    SDL_DisplayMode mode;
    if (display < 0 || SDL_GetCurrentDisplayMode(display, &mode) != 0) return 0;
    if (name && n) snprintf(name, n, "%s", SDL_GetDisplayName(display) ? SDL_GetDisplayName(display) : "display");
    return mode.refresh_rate > 0 ? (double)mode.refresh_rate : 0;
}
void *front_metal_layer(void) { return g_window; }
uint64_t front_game_res(int docked) {
    (void)docked;
    int w = atomic_load(&g_width), h = atomic_load(&g_height);
    if (w > 0 && h > 0) {
        double a = (double)(w > h ? w : h) / (double)(w > h ? h : w);
        unsigned short_side = render_height();
        unsigned width = (unsigned)(short_side * a / 8.0 + 0.5) * 8;
        if (width > 4096) width = 4096;
        return ((uint64_t)short_side << 32) | width;
    }
    return (uint64_t)900 << 32 | 1600;
}
int front_text_input(const char *title, const char *sub, const char *guide,
                     const char *initial, int max_chars, char *out, int out_cap) {
    (void)title; (void)sub; (void)guide;
    if (!g_window || !out || out_cap <= 0) return -1;
    pthread_mutex_lock(&g_text_mutex);
    while (g_text_requested || g_text_active) pthread_cond_wait(&g_text_cond, &g_text_mutex);
    g_text_done = g_text_accepted = 0;
    g_text_max_chars = max_chars;
    snprintf(g_text_title,sizeof g_text_title,"%s",title&&*title?title:"ENTER NAME");
    g_text_len = 0; g_text_buf[0] = 0;
    if (initial) text_append_locked(initial);
    g_text_requested = 1;
    pthread_cond_broadcast(&g_text_cond);
    while (!g_text_done && !atomic_load(&g_guest_done)) pthread_cond_wait(&g_text_cond, &g_text_mutex);
    int accepted = g_text_accepted;
    if (accepted) snprintf(out, (size_t)out_cap, "%s", g_text_buf);
    g_text_requested = g_text_active = g_text_done = 0;
    pthread_cond_broadcast(&g_text_cond);
    pthread_mutex_unlock(&g_text_mutex);
    dm_linux_ui_text_close();
    return accepted;
}
void front_save_failed(int err) {
    if (g_window) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Diablo III", "Save could not be written.", g_window);
    LOG("save write failed: errno=%d", err);
}

void front_run(void (*guest)(void)) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
        die("SDL_Init: %s", SDL_GetError());
    g_window = SDL_CreateWindow("Diablo III Port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    if (!g_window) die("SDL_CreateWindow: %s", SDL_GetError());
    SDL_SetWindowMinimumSize(g_window, 640, 360);
    int w = 0, h = 0; SDL_Vulkan_GetDrawableSize(g_window, &w, &h);
    atomic_store(&g_width, w); atomic_store(&g_height, h);
    dm_linux_vk_set_window(g_window);
    const char *shader_dir = getenv("DM_SHADER_DIR");
    char error[512] = {0};
    if (!shader_dir || !*shader_dir) die("Set DM_SHADER_DIR to the generated Vulkan .xgs bundle directory");
    int rc = dm_linux_vk_initialize(shader_dir, error, sizeof error);
    if (rc) die("Linux Vulkan initialization failed (%d): %s", rc, error);

    dm_linux_ui_init();
    g_guest = guest;
    pthread_t thread;
    if (pthread_create(&thread, NULL, guest_main, NULL)) die("Could not start game thread");
    SDL_GameControllerEventState(SDL_ENABLE);
    for (int i = 0; i < SDL_NumJoysticks() && !g_pad; ++i)
        if (SDL_IsGameController(i)) g_pad = SDL_GameControllerOpen(i);
    int running = 1;
    while (running) {
        text_begin_on_window_thread();
        char fallback[256]; size_t fallback_len = 0; int saw_text_event = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            int event_w = 0, event_h = 0; SDL_GetWindowSize(g_window, &event_w, &event_h);
            int text_ui_was_active=dm_linux_ui_text_active();
            int text_ui_handled=text_ui_was_active&&dm_linux_ui_text_event(&event,event_w,event_h);
            if(!text_ui_handled)text_event(&event, fallback, sizeof fallback, &fallback_len, &saw_text_event);
            if(!text_ui_was_active)dm_linux_ui_event(&event, event_w, event_h);
            if (event.type == SDL_QUIT) running = 0;
            if (event.type == SDL_CONTROLLERDEVICEADDED && !g_pad) g_pad = SDL_GameControllerOpen(event.cdevice.which);
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && g_pad &&
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad)) == event.cdevice.which) {
                SDL_GameControllerClose(g_pad); g_pad = NULL;
            }
            if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED)) {
                SDL_Vulkan_GetDrawableSize(g_window, &w, &h);
                atomic_store(&g_width, w); atomic_store(&g_height, h);
                dm_linux_vk_set_window(g_window);
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
        }
        // SDL_TEXTINPUT carries layout-aware Unicode. Steam's virtual keyboard
        // can instead inject ordinary key events; use those only when this poll
        // batch contained no text event, preventing duplicate characters.
        if (fallback_len && !saw_text_event) {
            fallback[fallback_len] = 0;
            pthread_mutex_lock(&g_text_mutex);
            if (g_text_active && !g_text_done) text_append_locked(fallback);
            pthread_mutex_unlock(&g_text_mutex);
        }
        publish_controller();
        SDL_Delay(4);
        if (atomic_load(&g_guest_done)) running = 0;
    }
    pthread_mutex_lock(&g_text_mutex);
    g_text_accepted = 0; g_text_done = 1; g_text_requested = g_text_active = 0;
    pthread_cond_broadcast(&g_text_cond);
    pthread_mutex_unlock(&g_text_mutex);
    SDL_StopTextInput();
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (!atomic_load(&g_guest_done)) SDL_Quit();
    else { pthread_join(thread, NULL); SDL_Quit(); }
}

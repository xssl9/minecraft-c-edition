#ifndef SDL_COMPAT_H
#define SDL_COMPAT_H

#include <stdint.h>
#include <stdbool.h>

typedef void* SDL_Window;
typedef void* SDL_Renderer;
typedef void* SDL_Texture;
typedef void* SDL_GLContext;
typedef int SDL_Keycode;

#define SDL_SCANCODE_W 'w'
#define SDL_SCANCODE_A 'a'
#define SDL_SCANCODE_S 's'
#define SDL_SCANCODE_D 'd'
#define SDL_SCANCODE_SPACE ' '
#define SDL_SCANCODE_LSHIFT 0x101
#define SDL_SCANCODE_RSHIFT 0x102
#define SDL_SCANCODE_LCTRL  0x103
#define SDL_SCANCODE_RCTRL  0x104

#define SDLK_ESCAPE 27
#define SDLK_SPACE ' '
#define SDLK_RETURN '\r'
#define SDLK_KP_ENTER '\r'
#define SDLK_BACKSPACE 8
#define SDLK_TAB '\t'
#define SDLK_1 '1'
#define SDLK_2 '2'
#define SDLK_3 '3'
#define SDLK_4 '4'
#define SDLK_5 '5'
#define SDLK_6 '6'
#define SDLK_7 '7'
#define SDLK_8 '8'
#define SDLK_9 '9'
#define SDLK_e 'e'
#define SDLK_t 't'
#define SDLK_SLASH '/'
#define SDLK_F1 0x201
#define SDLK_F2 0x202
#define SDLK_F3 0x203
#define SDLK_F4 0x204
#define SDLK_F5 0x205
#define SDLK_F11 0x20B
#define SDLK_m 'm'
#define SDLK_f 'f'

#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_MIDDLE 2

#define SDL_MESSAGEBOX_ERROR 1

#define KMOD_SHIFT 1
#define KMOD_CTRL 2

#define SDL_DISABLE 0
#define SDL_ENABLE 1
#define SDL_TRUE true
#define SDL_FALSE false
#define SDL_WINDOW_FULLSCREEN_DESKTOP 1

static inline void SDL_GL_SetSwapInterval(int i) { (void)i; }
static inline void SDL_SetWindowFullscreen(void* w, int f) { (void)w; (void)f; }

typedef uint64_t Uint64;
typedef uint8_t Uint8;

extern uint8_t g_mced_keys[512];
static inline const uint8_t* SDL_GetKeyboardState(int* numkeys) {
    if (numkeys) *numkeys = 512;
    return g_mced_keys;
}

static inline Uint64 SDL_GetPerformanceCounter(void) { return 0; }
static inline Uint64 SDL_GetPerformanceFrequency(void) { return 1000; }
static inline void SDL_Delay(uint32_t ms) { (void)ms; }
static inline int SDL_GetModState(void) { return 0; }
static inline void SDL_GetMouseState(int* x, int* y) { *x = 0; *y = 0; }

static inline void SDL_StartTextInput(void) {}
static inline void SDL_StopTextInput(void) {}
static inline void SDL_SetRelativeMouseMode(bool m) { (void)m; }
static inline void SDL_ShowCursor(int c) { (void)c; }
static inline uint32_t SDL_GetTicks(void) { return 0; }
static inline void SDL_GetWindowSize(void* w, int* ww, int* wh) { (void)w; *ww = 640; *wh = 480; }
static inline void SDL_WarpMouseInWindow(void* w, int x, int y) { (void)w; (void)x; (void)y; }
static inline void SDL_PumpEvents(void) {}
static inline void SDL_ShowSimpleMessageBox(int a, const char* b, const char* c, void* d) {
    (void)a; (void)b; (void)c; (void)d;
}

#endif

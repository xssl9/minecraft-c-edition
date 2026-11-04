#ifndef MCED_SETTINGS_H
#define MCED_SETTINGS_H

#include <stdbool.h>

typedef struct {
    int fov;                 /* 30 - 110, default 70 */
    int render_distance;     /* 2 - 32 chunks, default 16 */
    int max_fps;             /* 0 = Unlimited, 30, 60, 90, 120, 144, 240 */
    bool vsync;              /* default false */
    bool view_bobbing;       /* default true */
    bool clouds;             /* default true */
    bool fog;                /* default true */
    float brightness;        /* 0.0f (Moody) to 1.0f (Bright), default 0.5f */
    int gui_scale;           /* 0 = Auto, 1, 2, 3, 4 */
    bool fullscreen;         /* default false */

    float master_volume;     /* 0.0f - 1.0f, default 1.0f */
    float music_volume;      /* 0.0f - 1.0f, default 0.7f */
    float sound_volume;      /* 0.0f - 1.0f, default 1.0f */
    float blocks_volume;     /* 0.0f - 1.0f, default 1.0f */
    bool mute;               /* default false */

    float mouse_sensitivity; /* 0.2f - 3.0f, default 1.0f */
    bool invert_mouse;       /* default false */
} Settings;

void settings_init_defaults(Settings *s);
bool settings_load(Settings *s, const char *path);
bool settings_save(const Settings *s, const char *path);

#endif

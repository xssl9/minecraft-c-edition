#define MCED_DUNIT 1
#define MCED_USE_GL 0

#include "sdl_compat.h"
uint8_t g_mced_keys[512] = {0};

#include "stb_image.h"
#include "main.c"
#include "mced_dunit.h"

struct McedDunit {
    App app;
    int last_mouse_x;
    int last_mouse_y;
    bool has_mouse_pos;
    bool mouse_in_window;
    float key_timer[512];
};

static void apply_look(App *a, float dx, float dy) {
    float sens = 0.12f * a->settings.mouse_sensitivity;
    float pitch_mult = a->settings.invert_mouse ? -1.0f : 1.0f;
    a->player.yaw -= dx * sens;
    a->player.pitch += dy * sens * pitch_mult;
    if (a->player.pitch > 89.0f) a->player.pitch = 89.0f;
    if (a->player.pitch < -89.0f) a->player.pitch = -89.0f;
    while (a->player.yaw < 0) a->player.yaw += 360.0f;
    while (a->player.yaw >= 360.0f) a->player.yaw -= 360.0f;
}

McedDunit* mced_dunit_create(int width, int height, const char *asset_hint) {
    McedDunit *d = (McedDunit*)calloc(1, sizeof(McedDunit));
    if (!d) return NULL;

    App *a = &d->app;
    a->win_w = width;
    a->win_h = height;
    a->internal_w = width;
    a->internal_h = height;
    a->render_w = width;
    a->render_h = height;
    a->screenshot_frames = 3;
    a->running = true;
    a->fps = 60.0f;
    a->vsync = false;
    a->no_audio = true;
    a->render_distance = 2;

    if (!assets_load(&a->assets, asset_hint ? asset_hint : "./assets")) {
        free(d);
        return NULL;
    }
    blocks_init(&a->assets);

    if (!renderer_init(&a->renderer, &a->assets, a->internal_w, a->internal_h)) {
        assets_free(&a->assets);
        free(d);
        return NULL;
    }
    ui_init(&a->ui, a->renderer.ui_width, a->renderer.ui_height);

    a->pool = NULL;

    derive_paths(a);
    a->has_save = access(a->save_path, R_OK) == 0;
    settings_load(&a->settings, a->options_path);
    a->settings.render_distance = 2;
    a->settings.mute = true;

    ui_set_settings(&a->ui, &a->settings);
    apply_settings(a);

    ui_set_screen(&a->ui, UI_SCREEN_TITLE, a->has_save);

    return d;
}

McedDunit* mced_dunit_create_embedded(
    int width, int height,
    const uint8_t *atlas_png, size_t atlas_png_len,
    const char *atlas_txt, size_t atlas_txt_len,
    const uint8_t *font_alpha, size_t font_alpha_len
) {
    McedDunit *d = (McedDunit*)calloc(1, sizeof(McedDunit));
    if (!d) return NULL;

    App *a = &d->app;
    a->win_w = width;
    a->win_h = height;
    a->internal_w = width;
    a->internal_h = height;
    a->render_w = width;
    a->render_h = height;
    a->screenshot_frames = 3;
    a->running = true;
    a->fps = 60.0f;
    a->vsync = false;
    a->no_audio = true;
    a->render_distance = 2;

    Assets *assets = &a->assets;
    memset(assets, 0, sizeof(*assets));
    assets->atlas_size = MCED_ATLAS_SIZE;

    if (atlas_png && atlas_png_len > 0) {
        int w = 0, h = 0, ch = 0;
        assets->atlas_rgba = stbi_load_from_memory(atlas_png, (int)atlas_png_len, &w, &h, &ch, 4);
        if (w > 0) {
            assets->atlas_size = w;
        }
    }

    if (atlas_txt && atlas_txt_len > 0) {
        const char *p = atlas_txt;
        const char *end = atlas_txt + atlas_txt_len;
        char name[96];
        int tile = 0;
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p >= end) break;
            int n = 0;
            while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && n < 95) {
                name[n++] = *p++;
            }
            name[n] = '\0';
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            tile = 0;
            int sign = 1;
            if (p < end && *p == '-') { sign = -1; p++; }
            while (p < end && *p >= '0' && *p <= '9') {
                tile = tile * 10 + (*p++ - '0');
            }
            tile *= sign;
            assets_add_tile(assets, name, tile);
            while (p < end && *p != '\n') p++;
        }
    }
    if (font_alpha && font_alpha_len == sizeof(assets->font_alpha)) {
        memcpy(assets->font_alpha, font_alpha, sizeof(assets->font_alpha));
        assets->has_font = true;
    }

    blocks_init(assets);

    if (!renderer_init(&a->renderer, assets, a->internal_w, a->internal_h)) {
        assets_free(assets);
        free(d);
        return NULL;
    }
    ui_init(&a->ui, a->renderer.ui_width, a->renderer.ui_height);

    a->pool = NULL;

    derive_paths(a);
    a->has_save = access(a->save_path, R_OK) == 0;
    settings_load(&a->settings, a->options_path);
    a->settings.render_distance = 2;
    a->settings.mute = true;

    ui_set_settings(&a->ui, &a->settings);
    apply_settings(a);

    ui_set_screen(&a->ui, UI_SCREEN_TITLE, a->has_save);

    return d;
}

void mced_dunit_destroy(McedDunit *d) {
    if (!d) return;
    shutdown_app(&d->app);
    free(d);
}

void mced_dunit_start_world(McedDunit *d, bool new_world) {
    if (!d) return;
    start_world(&d->app, new_world);
}

void mced_dunit_update(McedDunit *d, float dt) {
    if (!d) return;
    App *a = &d->app;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0001f) dt = 0.0001f;

    for (int i = 0; i < 512; i++) {
        if (d->key_timer[i] > 0.0f) {
            d->key_timer[i] -= dt;
            if (d->key_timer[i] <= 0.0f) {
                d->key_timer[i] = 0.0f;
                g_mced_keys[i] = 0;
            }
        }
    }

    a->fps = a->fps * 0.92f + (1.0f / dt) * 0.08f;

    if (!a->world_active &&
        a->ui.screen != UI_SCREEN_TITLE &&
        a->ui.screen != UI_SCREEN_CREATE_WORLD &&
        a->ui.screen != UI_SCREEN_OPTIONS &&
        a->ui.screen != UI_SCREEN_VIDEO &&
        a->ui.screen != UI_SCREEN_AUDIO &&
        a->ui.screen != UI_SCREEN_CONTROLS) {
        ui_set_screen(&a->ui, UI_SCREEN_TITLE, a->has_save);
    }

    if (a->world_active && (a->ui.screen == UI_SCREEN_NONE || a->ui.screen == UI_SCREEN_CHAT)) {
        update_playing(a, dt);
    } else if (a->toast_timer > 0) {
        a->toast_timer -= dt;
    }
}

void mced_dunit_resize(McedDunit *d, int width, int height) {
    if (!d || width <= 0 || height <= 0) return;
    App *a = &d->app;
    if (a->win_w == width && a->win_h == height && a->render_w == width && a->render_h == height) {
        return;
    }

    a->win_w = width;
    a->win_h = height;
    a->internal_w = width;
    a->internal_h = height;
    a->render_w = width;
    a->render_h = height;

    renderer_resize(&a->renderer, width, height);

    a->ui.width = a->renderer.ui_width;
    a->ui.height = a->renderer.ui_height;
    ui_set_screen(&a->ui, a->ui.screen, a->has_save);
}

const uint32_t* mced_dunit_render(McedDunit *d) {
    if (!d) return NULL;
    render_frame(&d->app);
    if (!d->app.renderer.zb) return NULL;
    return (const uint32_t*)d->app.renderer.zb->pbuf;
}

void mced_dunit_key(McedDunit *d, int scancode, int key_char, bool down) {
    if (!d) return;
    App *a = &d->app;

    // Movement key mappings for SDL keyboard state:
    // PS/2 scancodes: 17=W, 30=A, 31=S, 32=D, 57=Space, 42/54=Shift, 29=Ctrl
    if (scancode == 17 || key_char == 'w' || key_char == 'W')
        g_mced_keys[SDL_SCANCODE_W] = down ? 1 : 0;
    if (scancode == 30 || key_char == 'a' || key_char == 'A')
        g_mced_keys[SDL_SCANCODE_A] = down ? 1 : 0;
    if (scancode == 31 || key_char == 's' || key_char == 'S')
        g_mced_keys[SDL_SCANCODE_S] = down ? 1 : 0;
    if (scancode == 32 || key_char == 'd' || key_char == 'D')
        g_mced_keys[SDL_SCANCODE_D] = down ? 1 : 0;
    if (scancode == 57 || key_char == ' ')
        g_mced_keys[SDL_SCANCODE_SPACE] = down ? 1 : 0;
    if (scancode == 42 || scancode == 54) {
        g_mced_keys[SDL_SCANCODE_LSHIFT] = down ? 1 : 0;
        g_mced_keys[SDL_SCANCODE_RSHIFT] = down ? 1 : 0;
    }
    if (scancode == 29) {
        g_mced_keys[SDL_SCANCODE_LCTRL] = down ? 1 : 0;
        g_mced_keys[SDL_SCANCODE_RCTRL] = down ? 1 : 0;
    }

    if (scancode >= 0 && scancode < 512) {
        g_mced_keys[scancode] = down ? 1 : 0;
        d->key_timer[scancode] = down ? 0.20f : 0.0f;
    }
    if (key_char >= 0 && key_char < 512) {
        g_mced_keys[key_char] = down ? 1 : 0;
        d->key_timer[key_char] = down ? 0.20f : 0.0f;
        if (key_char >= 'A' && key_char <= 'Z') {
            int lower = key_char + 32;
            g_mced_keys[lower] = down ? 1 : 0;
            d->key_timer[lower] = down ? 0.20f : 0.0f;
        }
    }

    if (down) {
        // Chat mode text entry
        if (a->ui.screen == UI_SCREEN_CHAT) {
            if (scancode == 1 || key_char == 27) {
                // Escape: close chat
                ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
            } else if (scancode == 28 || key_char == 10 || key_char == 13) {
                // Enter: execute command / send message
                execute_chat_command(a, a->ui.chat_input);
                ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
            } else if (scancode == 14 || key_char == 8 || key_char == 127) {
                // Backspace
                ui_chat_backspace(&a->ui);
            } else if (key_char >= 32 && key_char <= 126) {
                if (key_char == '/' && a->ignore_slash_text) {
                    a->ignore_slash_text = false;
                } else {
                    a->ignore_slash_text = false;
                    ui_chat_input_char(&a->ui, (char)key_char);
                }
            }
            return;
        }

        // Create World text fields
        if (a->ui.screen == UI_SCREEN_CREATE_WORLD) {
            if (scancode == 1 || key_char == 27) {
                handle_key(a, SDLK_ESCAPE, true);
            } else if (scancode == 28 || key_char == 10 || key_char == 13) {
                handle_key(a, SDLK_RETURN, true);
            } else if (scancode == 14 || key_char == 8 || key_char == 127) {
                handle_key(a, SDLK_BACKSPACE, true);
            } else if (key_char >= 32 && key_char <= 126) {
                ui_text_input_char(&a->ui, (char)key_char);
            }
            return;
        }

        int key = key_char ? key_char : scancode;
        if (key_char >= 'A' && key_char <= 'Z') key = key_char + 32;

        if (scancode == 1) key = SDLK_ESCAPE;
        else if (scancode == 28) key = SDLK_RETURN;
        else if (scancode == 14) key = SDLK_BACKSPACE;
        else if (scancode == 60) key = SDLK_F2;
        else if (scancode == 61) key = SDLK_F3;
        else if (scancode == 62) key = SDLK_F4;
        else if (scancode == 63) key = SDLK_F5;
        else if (scancode == 87) key = SDLK_F11;

        if (a->ui.screen == UI_SCREEN_TITLE && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)) {
            start_world(a, true);
        } else {
            handle_key(a, key, true);
        }
    }
}

void mced_dunit_mouse_move(McedDunit *d, int x, int y) {
    if (!d) return;
    App *a = &d->app;

    if (!d->has_mouse_pos || !d->mouse_in_window) {
        d->last_mouse_x = x;
        d->last_mouse_y = y;
        d->has_mouse_pos = true;
        d->mouse_in_window = true;
        return;
    }

    int dx = x - d->last_mouse_x;
    int dy = y - d->last_mouse_y;
    d->last_mouse_x = x;
    d->last_mouse_y = y;

    if (a->world_active && a->ui.screen == UI_SCREEN_NONE) {
        // Discard absurd jumps caused by cursor leave/re-enter or edge wraps
        if (abs(dx) > a->render_w / 2 || abs(dy) > a->render_h / 2) {
            return;
        }
        apply_look(a, (float)dx, (float)dy);
    } else {
        float ui_x = (float)x;
        float ui_y = (float)y;
        if (a->renderer.ui_scale > 0.0f) {
            ui_x /= a->renderer.ui_scale;
            ui_y /= a->renderer.ui_scale;
        }
        ui_mouse_move(&a->ui, ui_x, ui_y);
        if (a->ui.active_slider >= 0) {
            apply_settings(a);
        }
    }
}

void mced_dunit_mouse_leave(McedDunit *d) {
    if (!d) return;
    d->has_mouse_pos = false;
    d->mouse_in_window = false;
}

void mced_dunit_mouse_look(McedDunit *d, float dx, float dy) {
    if (!d) return;
    App *a = &d->app;
    if (a->world_active && a->ui.screen == UI_SCREEN_NONE) {
        apply_look(a, dx, dy);
    }
}

void mced_dunit_mouse_button(McedDunit *d, int button, bool down) {
    if (!d) return;
    App *a = &d->app;

    if (button == 0) button = SDL_BUTTON_LEFT;
    else if (button == 1) button = SDL_BUTTON_RIGHT;

    float ui_x = (float)d->last_mouse_x;
    float ui_y = (float)d->last_mouse_y;
    if (a->renderer.ui_scale > 0.0f) {
        ui_x /= a->renderer.ui_scale;
        ui_y /= a->renderer.ui_scale;
    }

    if (button == SDL_BUTTON_LEFT) {
        if (down) {
            if (a->ui.screen == UI_SCREEN_NONE && a->world_active) {
                a->mouse_left = true;
            } else if (a->ui.screen == UI_SCREEN_INVENTORY) {
                ui_slot_click_event(&a->ui, &a->player, false, false);
            } else if (a->ui.screen != UI_SCREEN_LOADING) {
                if (ui_mouse_down(&a->ui, ui_x, ui_y)) {
                    // slider activated
                } else {
                    UIAction action = ui_click(&a->ui, ui_x, ui_y, &a->player);
                    if (action != UI_ACT_NONE)
                        handle_action(a, action);
                }
            }
        } else {
            a->mouse_left = false;
            if (a->ui.active_slider >= 0) {
                ui_mouse_up(&a->ui);
                apply_settings(a);
                settings_save(&a->settings, a->options_path);
            }
        }
    } else if (button == SDL_BUTTON_RIGHT) {
        if (down) {
            if (a->ui.screen == UI_SCREEN_NONE && a->world_active) {
                a->mouse_right = true;
            } else if (a->ui.screen == UI_SCREEN_INVENTORY) {
                ui_slot_click_event(&a->ui, &a->player, true, false);
            }
        } else {
            a->mouse_right = false;
        }
    }
}

void mced_dunit_mouse_scroll(McedDunit *d, int delta) {
    if (!d) return;
    App *a = &d->app;
    if (a->ui.screen == UI_SCREEN_INVENTORY) {
        ui_scroll_inventory(&a->ui, -delta);
    } else if (a->ui.screen == UI_SCREEN_NONE && a->world_active) {
        int dir = delta > 0 ? -1 : 1;
        a->player.selected = (a->player.selected + dir + HOTBAR_SLOTS) % HOTBAR_SLOTS;
    }
}

bool mced_dunit_is_in_world(McedDunit *d) {
    return d && d->app.world_active && d->app.ui.screen == UI_SCREEN_NONE;
}

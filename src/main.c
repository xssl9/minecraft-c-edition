#include "assets.h"
#include "audio.h"
#include "blocks.h"
#include "mced.h"
#include "player.h"
#include "render.h"
#include "settings.h"
#include "ui.h"
#include "world.h"
#include "entity.h"
#include "thread_pool.h"

#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    SDL_Window *window;
#if MCED_USE_GL
    SDL_GLContext gl_context;
#endif
    SDL_Renderer *sdl;
    SDL_Texture *screen;
    bool headless;
    bool vsync;
    int max_fps;
    int render_w, render_h;
    int win_w, win_h;
    int internal_w, internal_h;

    Assets assets;
    Renderer renderer;
    World world;
    Player player;
    UI ui;
    Audio audio;
    EntityManager entities;
    ThreadPool *pool;

    bool running;
    bool world_active;
    bool has_save;
    bool no_audio;
    bool fullscreen;
    bool screenshot_menu;
    bool play_direct;
    bool no_save;
    int render_distance;
    const char *screenshot_path;
    const char *screenshot_screen;
    int screenshot_frames;
    int frame_count;
    uint64_t seed_override;
    bool has_seed_override;
    float forced_yaw, forced_pitch;
    bool has_yaw, has_pitch;
    float eye_override[3];
    bool has_eye_override;
    float time_override;
    bool has_time_override;
    const char *scene_name;
    int inv_page_override;
    int held_item_override;
    bool has_held_override;

    char project_dir[512];
    char save_dir[560];
    char save_path[640];
    char player_path[640];
    char options_path[640];
    char screenshot_dir[560];
    Settings settings;

    float fps;
    float save_timer;
    float cave_timer;
    float toast_timer;
    char toast[160];
    HUDInfo hud;
    bool mouse_left, mouse_right;
    bool ignore_slash_text;
    float step_accum;
    Vec3 last_step_pos;

    float world_time;
#define MAX_DROPPED_ITEMS 128
    struct {
        Vec3 pos;
        Vec3 vel;
        uint8_t block;
        uint8_t count;
        float age;
        float rot;
        bool active;
    } dropped_items[MAX_DROPPED_ITEMS];
    int dropped_count;

#define MAX_ACTIVE_TNT 32
    struct {
        Vec3 pos;
        Vec3 vel;
        float fuse;
        bool active;
    } active_tnt[MAX_ACTIVE_TNT];
    int active_tnt_count;

    /* XWayland's relative mouse mode can feed SDL's own cursor warps back
       as motion, which makes the camera spin on its own.  In that case we
       track the absolute cursor instead and recenter it at the edges. */
    bool soft_look;
    bool force_relative_mouse;
    bool look_anchor;
    bool look_warp_pending;
    uint32_t look_warp_ticks;
    int look_last_x, look_last_y;
} App;

static void spawn_dropped_item(App *a, uint8_t block, int bx, int by, int bz);

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Minecraft C Edition", msg, NULL);
    exit(1);
}

static void derive_paths(App *a) {
    snprintf(a->project_dir, sizeof(a->project_dir), ".");
    const char *env = getenv("MCED_HOME");
    if (env && env[0]) {
        snprintf(a->project_dir, sizeof(a->project_dir), "%s", env);
    } else {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", a->assets.root);
        char *slash = strrchr(buf, '/');
        if (slash && strcmp(slash + 1, "assets") == 0) {
            *slash = '\0';
            snprintf(a->project_dir, sizeof(a->project_dir), "%s", buf);
        }
    }
    snprintf(a->save_dir, sizeof(a->save_dir), "%s/saves", a->project_dir);
    snprintf(a->screenshot_dir, sizeof(a->screenshot_dir), "%s/screenshots", a->project_dir);
    snprintf(a->save_path, sizeof(a->save_path), "%s/world.mced", a->save_dir);
    snprintf(a->player_path, sizeof(a->player_path), "%s/player.dat", a->save_dir);
    snprintf(a->options_path, sizeof(a->options_path), "%s/options.txt", a->save_dir);
    mkdir(a->save_dir, 0755);
    mkdir(a->screenshot_dir, 0755);
}

static void set_toast(App *a, const char *text) {
    snprintf(a->toast, sizeof(a->toast), "%s", text);
    a->toast_timer = 2.4f;
}

static bool player_save(App *a) {
    FILE *f = fopen(a->player_path, "wb");
    if (!f)
        return false;
    const char magic[4] = {'M', 'C', 'P', '1'};
    uint32_t version = 3;
    float px = a->player.pos.x, py = a->player.pos.y, pz = a->player.pos.z;
    fwrite(magic, 1, 4, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&px, sizeof(px), 1, f);
    fwrite(&py, sizeof(py), 1, f);
    fwrite(&pz, sizeof(pz), 1, f);
    fwrite(&a->player.yaw, sizeof(float), 1, f);
    fwrite(&a->player.pitch, sizeof(float), 1, f);
    fwrite(&a->player.flying, sizeof(bool), 1, f);
    int32_t sel = a->player.selected;
    fwrite(&sel, sizeof(sel), 1, f);
    fwrite(a->player.hotbar, 1, HOTBAR_SLOTS, f);
    fwrite(&a->player.health, sizeof(float), 1, f);
    fwrite(&a->player.air, sizeof(float), 1, f);
    fwrite(a->player.hotbar_counts, 1, HOTBAR_SLOTS, f);
    fwrite(a->player.inventory, 1, INVENTORY_SLOTS, f);
    fwrite(a->player.inventory_counts, 1, INVENTORY_SLOTS, f);
    fwrite(a->player.armor, 1, ARMOR_SLOTS, f);
    fwrite(&a->player.hunger, sizeof(float), 1, f);
    fwrite(&a->player.saturation, sizeof(float), 1, f);
    fwrite(&a->player.level, sizeof(int), 1, f);
    fwrite(&a->player.exp_progress, sizeof(float), 1, f);
    fwrite(&a->player.creative, sizeof(bool), 1, f);
    fclose(f);
    return true;
}

static bool player_load(App *a) {
    FILE *f = fopen(a->player_path, "rb");
    if (!f)
        return false;
    char magic[4];
    uint32_t version = 0;
    float px, py, pz;
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "MCP1", 4) == 0 &&
              fread(&version, sizeof(version), 1, f) == 1 &&
              (version >= 1 && version <= 3) &&
              fread(&px, sizeof(px), 1, f) == 1 &&
              fread(&py, sizeof(py), 1, f) == 1 &&
              fread(&pz, sizeof(pz), 1, f) == 1 &&
              fread(&a->player.yaw, sizeof(float), 1, f) == 1 &&
              fread(&a->player.pitch, sizeof(float), 1, f) == 1 &&
              fread(&a->player.flying, sizeof(bool), 1, f) == 1;
    int32_t sel = 0;
    if (ok)
        ok = fread(&sel, sizeof(sel), 1, f) == 1 &&
             fread(a->player.hotbar, 1, HOTBAR_SLOTS, f) == HOTBAR_SLOTS;
    float health = PLAYER_MAX_HEALTH, air = PLAYER_MAX_AIR;
    if (ok && version >= 2) {
        ok = fread(&health, sizeof(float), 1, f) == 1 &&
             fread(&air, sizeof(float), 1, f) == 1;
    }
    if (ok && version >= 3) {
        ok = fread(a->player.hotbar_counts, 1, HOTBAR_SLOTS, f) == HOTBAR_SLOTS &&
             fread(a->player.inventory, 1, INVENTORY_SLOTS, f) == INVENTORY_SLOTS &&
             fread(a->player.inventory_counts, 1, INVENTORY_SLOTS, f) == INVENTORY_SLOTS &&
             fread(a->player.armor, 1, ARMOR_SLOTS, f) == ARMOR_SLOTS &&
             fread(&a->player.hunger, sizeof(float), 1, f) == 1 &&
             fread(&a->player.saturation, sizeof(float), 1, f) == 1 &&
             fread(&a->player.level, sizeof(int), 1, f) == 1 &&
             fread(&a->player.exp_progress, sizeof(float), 1, f) == 1 &&
             fread(&a->player.creative, sizeof(bool), 1, f) == 1;
    }
    fclose(f);
    if (!ok)
        return false;
    if (health <= 0 || health > PLAYER_MAX_HEALTH)
        health = PLAYER_MAX_HEALTH;
    if (air < 0 || air > PLAYER_MAX_AIR)
        air = PLAYER_MAX_AIR;
    a->player.health = health;
    a->player.air = air;
    if (px < -30000000 || px > 30000000 || pz < -30000000 || pz > 30000000 ||
        py < -10 || py > WORLD_HEIGHT + 20)
        return false;
    a->player.pos = vec3(px, py, pz);
    a->player.selected = clampi(sel, 0, HOTBAR_SLOTS - 1);
    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        if (a->player.hotbar[i] >= BLOCK_COUNT)
            a->player.hotbar[i] = BLOCK_STONE;
        if (version < 3 && a->player.hotbar[i] != BLOCK_AIR && a->player.hotbar_counts[i] == 0)
            a->player.hotbar_counts[i] = 64;
    }
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        if (a->player.inventory[i] >= BLOCK_COUNT)
            a->player.inventory[i] = BLOCK_AIR;
    }
    return true;
}

static void save_world(App *a, bool force) {
    if (!a->world_active || a->headless || a->no_save)
        return;
    if (!force && !a->world.dirty)
        return;
    char tmp[700];
    snprintf(tmp, sizeof(tmp), "%s.tmp", a->save_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        fprintf(stderr, "could not write save: %s\n", strerror(errno));
        return;
    }
    bool ok = world_write(&a->world, f);
    fclose(f);
    if (!ok) {
        remove(tmp);
        fprintf(stderr, "world write failed\n");
        return;
    }
    if (rename(tmp, a->save_path) != 0)
        remove(tmp);
    player_save(a);
    a->world.dirty = false;
    a->has_save = true;
}

static void update_mouse_capture(App *a) {
    if (a->headless)
        return;
    bool play = a->world_active && a->ui.screen == UI_SCREEN_NONE;
    if (a->soft_look) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(play ? SDL_DISABLE : SDL_ENABLE);
        a->look_warp_pending = false;
        a->look_anchor = false;
        if (play) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            const int margin = 24;
            int bound_w = a->win_w > 0 ? a->win_w : a->internal_w;
            int bound_h = a->win_h > 0 ? a->win_h : a->internal_h;
            if (mx < margin || mx >= bound_w - margin ||
                my < margin || my >= bound_h - margin) {
                int ww = 0, wh = 0;
                SDL_GetWindowSize(a->window, &ww, &wh);
                SDL_WarpMouseInWindow(a->window, ww / 2, wh / 2);
                /* Let the warp settle before accepting motion, then anchor
                   on the first real event instead of guessing its position. */
                a->look_warp_pending = true;
                a->look_warp_ticks = SDL_GetTicks();
            }
        }
        return;
    }
    SDL_SetRelativeMouseMode(play ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(play ? SDL_DISABLE : SDL_ENABLE);
}

static void present_screen(App *a) {
    if (a->headless)
        return;
#if MCED_USE_GL
    SDL_GL_SwapWindow(a->window);
#else
    SDL_UpdateTexture(a->screen, NULL, a->renderer.zb->pbuf, a->renderer.zb->linesize);
    SDL_RenderClear(a->sdl);
    SDL_RenderCopy(a->sdl, a->screen, NULL, NULL);
    SDL_RenderPresent(a->sdl);
#endif
}

static void present_loading(App *a, const char *status, float progress) {
    renderer_clear(&a->renderer);
    ui_draw_loading(&a->renderer, status, progress);
    present_screen(a);
    if (!a->headless)
        SDL_PumpEvents();
}

static uint64_t make_seed(App *a) {
    if (a->has_seed_override)
        return a->seed_override;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t s = (uint64_t)ts.tv_sec * 1000000007ULL ^ (uint64_t)ts.tv_nsec;
    s ^= (uint64_t)(uintptr_t)a;
    return mced_hash64(s);
}

static void enter_title(App *a) {
    if (a->world_active) {
        save_world(a, false);
        world_destroy(&a->world);
        renderer_reset_chunks(&a->renderer);
        a->world_active = false;
    }
    a->has_save = access(a->save_path, R_OK) == 0;
    ui_set_screen(&a->ui, UI_SCREEN_TITLE, a->has_save);
    audio_music_menu(&a->audio, true);
    update_mouse_capture(a);
}

static void setup_scene(App *a, const char *scene) {
    if (!scene) return;
    int sx = (int)floorf(a->player.pos.x);
    int sy = (int)floorf(a->player.pos.y);
    int sz = (int)floorf(a->player.pos.z);

    if (!strcmp(scene, "redstone")) {
        for (int dx = -5; dx <= 5; dx++) {
            for (int dz = 2; dz <= 10; dz++) {
                for (int dy = 0; dy <= 4; dy++)
                    world_set_block(&a->world, sx + dx, sy + dy, sz + dz, BLOCK_AIR, false);
                world_set_block(&a->world, sx + dx, sy - 1, sz + dz, BLOCK_STONE, false);
            }
        }
        world_set_block(&a->world, sx - 3, sy, sz + 6, BLOCK_REDSTONE_BLOCK, false);
        world_set_block(&a->world, sx - 2, sy, sz + 6, BLOCK_REDSTONE_WIRE, false);
        world_set_block(&a->world, sx - 1, sy, sz + 6, BLOCK_REDSTONE_WIRE, false);
        world_set_block(&a->world, sx, sy, sz + 6, BLOCK_REDSTONE_LAMP_ON, false);
        world_set_power(&a->world, sx, sy, sz + 6, 15);

        world_set_block(&a->world, sx - 2, sy, sz + 8, BLOCK_REDSTONE_WIRE, false);
        world_set_block(&a->world, sx - 1, sy, sz + 8, BLOCK_REDSTONE_WIRE, false);
        world_set_block(&a->world, sx, sy, sz + 8, BLOCK_REDSTONE_LAMP_OFF, false);

        world_set_block(&a->world, sx + 2, sy, sz + 5, BLOCK_STONE, false);
        world_set_block(&a->world, sx + 2, sy + 1, sz + 5, BLOCK_LEVER, false);
        world_set_power(&a->world, sx + 2, sy + 1, sz + 5, 15);
        world_set_block(&a->world, sx + 3, sy, sz + 5, BLOCK_IRON_DOOR_OPEN, false);

        world_set_block(&a->world, sx + 2, sy, sz + 7, BLOCK_STONE, false);
        world_set_block(&a->world, sx + 2, sy + 1, sz + 7, BLOCK_STONE_BUTTON, false);
        world_set_block(&a->world, sx + 3, sy, sz + 7, BLOCK_OAK_DOOR, false);

        world_set_block(&a->world, sx - 3, sy, sz + 4, BLOCK_STONE, false);
        world_set_block(&a->world, sx - 3, sy + 1, sz + 4, BLOCK_REDSTONE_TORCH, false);
        world_set_power(&a->world, sx - 3, sy + 1, sz + 4, 15);

        a->player.pos = vec3((float)sx, (float)sy + 0.2f, (float)sz + 2.0f);
        a->player.yaw = 180.0f;
        a->player.pitch = -12.0f;
    } else if (!strcmp(scene, "liquids")) {
        for (int dx = -6; dx <= 6; dx++) {
            for (int dz = 2; dz <= 12; dz++) {
                for (int dy = 0; dy <= 6; dy++)
                    world_set_block(&a->world, sx + dx, sy + dy, sz + dz, BLOCK_AIR, false);
                world_set_block(&a->world, sx + dx, sy - 1, sz + dz, BLOCK_COBBLESTONE, false);
            }
        }
        world_set_block(&a->world, sx - 3, sy, sz + 7, BLOCK_COBBLESTONE, false);
        world_set_block(&a->world, sx - 3, sy + 1, sz + 7, BLOCK_COBBLESTONE, false);
        world_set_block(&a->world, sx - 3, sy + 2, sz + 7, BLOCK_COBBLESTONE, false);
        world_set_block(&a->world, sx - 3, sy + 3, sz + 7, BLOCK_WATER, false);

        world_set_block(&a->world, sx + 3, sy, sz + 7, BLOCK_COBBLESTONE, false);
        world_set_block(&a->world, sx + 3, sy + 1, sz + 7, BLOCK_COBBLESTONE, false);
        world_set_block(&a->world, sx + 3, sy + 2, sz + 7, BLOCK_COBBLESTONE, false);
        world_set_block(&a->world, sx + 3, sy + 3, sz + 7, BLOCK_LAVA, false);

        world_set_block(&a->world, sx, sy, sz + 7, BLOCK_OBSIDIAN, false);

        a->player.pos = vec3((float)sx, (float)sy + 0.8f, (float)sz + 2.5f);
        a->player.yaw = 180.0f;
        a->player.pitch = -10.0f;
    } else if (!strcmp(scene, "architecture") || !strcmp(scene, "slabs_stairs")) {
        for (int dx = -6; dx <= 6; dx++) {
            for (int dz = 2; dz <= 12; dz++) {
                for (int dy = 0; dy <= 6; dy++)
                    world_set_block(&a->world, sx + dx, sy + dy, sz + dz, BLOCK_AIR, false);
                world_set_block(&a->world, sx + dx, sy - 1, sz + dz, BLOCK_STONE_BRICKS, false);
            }
        }
        for (int dx = -3; dx <= 3; dx++) {
            world_set_block(&a->world, sx + dx, sy, sz + 8, BLOCK_OAK_STAIRS, false);
            world_set_block(&a->world, sx + dx, sy + 1, sz + 8, BLOCK_OAK_SLAB, false);
        }
        world_set_block(&a->world, sx - 2, sy, sz + 6, BLOCK_CRAFTING_TABLE, false);
        world_set_block(&a->world, sx - 1, sy, sz + 6, BLOCK_FURNACE, false);
        world_set_block(&a->world, sx, sy, sz + 6, BLOCK_CHEST, false);
        world_set_block(&a->world, sx + 1, sy, sz + 6, BLOCK_BOOKSHELF, false);
        world_set_block(&a->world, sx + 2, sy, sz + 6, BLOCK_BOOKSHELF, false);

        world_set_block(&a->world, sx - 2, sy + 1, sz + 6, BLOCK_TORCH, false);
        world_set_block(&a->world, sx + 2, sy + 1, sz + 6, BLOCK_TORCH, false);

        a->player.pos = vec3((float)sx, (float)sy + 0.5f, (float)sz + 2.5f);
        a->player.yaw = 180.0f;
        a->player.pitch = -12.0f;
    } else if (!strcmp(scene, "flora")) {
        for (int dx = -6; dx <= 6; dx++) {
            for (int dz = 2; dz <= 12; dz++) {
                for (int dy = 0; dy <= 6; dy++)
                    world_set_block(&a->world, sx + dx, sy + dy, sz + dz, BLOCK_AIR, false);
                world_set_block(&a->world, sx + dx, sy - 1, sz + dz, BLOCK_GRASS_BLOCK, false);
            }
        }
        for (int dz = 4; dz <= 9; dz++) {
            world_set_block(&a->world, sx - 2, sy - 1, sz + dz, BLOCK_WATER, false);
            world_set_block(&a->world, sx - 1, sy - 1, sz + dz, BLOCK_SAND, false);
            world_set_block(&a->world, sx - 1, sy, sz + dz, BLOCK_SUGAR_CANE, false);
            world_set_block(&a->world, sx - 1, sy + 1, sz + dz, BLOCK_SUGAR_CANE, false);
        }
        for (int dx = 2; dx <= 4; dx++) {
            for (int dz = 5; dz <= 8; dz++) {
                world_set_block(&a->world, sx + dx, sy - 1, sz + dz, BLOCK_SAND, false);
            }
        }
        world_set_block(&a->world, sx + 3, sy, sz + 6, BLOCK_CACTUS, false);
        world_set_block(&a->world, sx + 3, sy + 1, sz + 6, BLOCK_CACTUS, false);
        world_set_block(&a->world, sx + 3, sy + 2, sz + 6, BLOCK_CACTUS, false);
        world_set_block(&a->world, sx + 2, sy, sz + 7, BLOCK_DEAD_BUSH, false);

        world_set_block(&a->world, sx, sy, sz + 5, BLOCK_POPPY, false);
        world_set_block(&a->world, sx + 1, sy, sz + 5, BLOCK_DANDELION, false);
        world_set_block(&a->world, sx, sy, sz + 7, BLOCK_SHORT_GRASS, false);
        world_set_block(&a->world, sx + 1, sy, sz + 7, BLOCK_POPPY, false);

        a->player.armor[0] = ITEM_DIAMOND_HELMET;
        a->player.armor[1] = ITEM_DIAMOND_CHESTPLATE;
        a->player.armor[2] = ITEM_DIAMOND_LEGGINGS;
        a->player.armor[3] = ITEM_DIAMOND_BOOTS;
        a->player.hotbar[0] = ITEM_DIAMOND_SWORD;
        a->player.hotbar_counts[0] = 1;

        a->player.pos = vec3((float)sx, (float)sy + 0.5f, (float)sz + 2.0f);
        a->player.yaw = 180.0f;
        a->player.pitch = -12.0f;
    } else if (!strcmp(scene, "forest") || !strcmp(scene, "trees")) {
        for (int dx = -10; dx <= 10; dx++) {
            for (int dz = 2; dz <= 18; dz++) {
                for (int dy = 0; dy <= 12; dy++)
                    world_set_block(&a->world, sx + dx, sy + dy, sz + dz, BLOCK_AIR, false);
                world_set_block(&a->world, sx + dx, sy - 1, sz + dz, BLOCK_GRASS_BLOCK, false);
            }
        }
        for (int dx = -10; dx <= 10; dx++) {
            for (int dy = 0; dy <= 8; dy++) {
                world_set_block(&a->world, sx + dx, sy + dy, sz + 14, BLOCK_STONE, false);
                world_set_block(&a->world, sx + dx, sy + dy, sz + 15, BLOCK_DIRT, false);
            }
        }
        for (int y = 0; y < 5; y++) world_set_block(&a->world, sx - 4, sy + y, sz + 8, BLOCK_OAK_LOG, false);
        for (int lx = -2; lx <= 2; lx++) {
            for (int lz = -2; lz <= 2; lz++) {
                for (int ly = 3; ly <= 4; ly++) {
                    if (world_get_block(&a->world, sx - 4 + lx, sy + ly, sz + 8 + lz) == BLOCK_AIR)
                        world_set_block(&a->world, sx - 4 + lx, sy + ly, sz + 8 + lz, BLOCK_OAK_LEAVES, false);
                }
            }
        }
        for (int lx = -1; lx <= 1; lx++) {
            for (int lz = -1; lz <= 1; lz++) {
                for (int ly = 5; ly <= 6; ly++) {
                    if (world_get_block(&a->world, sx - 4 + lx, sy + ly, sz + 8 + lz) == BLOCK_AIR)
                        world_set_block(&a->world, sx - 4 + lx, sy + ly, sz + 8 + lz, BLOCK_OAK_LEAVES, false);
                }
            }
        }
        for (int y = 0; y < 6; y++) world_set_block(&a->world, sx + 4, sy + y, sz + 9, BLOCK_BIRCH_LOG, false);
        for (int lx = -2; lx <= 2; lx++) {
            for (int lz = -2; lz <= 2; lz++) {
                for (int ly = 4; ly <= 5; ly++) {
                    if (world_get_block(&a->world, sx + 4 + lx, sy + ly, sz + 9 + lz) == BLOCK_AIR)
                        world_set_block(&a->world, sx + 4 + lx, sy + ly, sz + 9 + lz, BLOCK_BIRCH_LEAVES, false);
                }
            }
        }
        for (int lx = -1; lx <= 1; lx++) {
            for (int lz = -1; lz <= 1; lz++) {
                for (int ly = 6; ly <= 7; ly++) {
                    if (world_get_block(&a->world, sx + 4 + lx, sy + ly, sz + 9 + lz) == BLOCK_AIR)
                        world_set_block(&a->world, sx + 4 + lx, sy + ly, sz + 9 + lz, BLOCK_BIRCH_LEAVES, false);
                }
            }
        }
        for (int y = 0; y < 5; y++) world_set_block(&a->world, sx, sy + y, sz + 12, BLOCK_OAK_LOG, false);
        for (int lx = -2; lx <= 2; lx++) {
            for (int lz = -2; lz <= 2; lz++) {
                for (int ly = 3; ly <= 4; ly++) {
                    if (world_get_block(&a->world, sx + lx, sy + ly, sz + 12 + lz) == BLOCK_AIR)
                        world_set_block(&a->world, sx + lx, sy + ly, sz + 12 + lz, BLOCK_OAK_LEAVES, false);
                }
            }
        }
        a->player.pos = vec3((float)sx, (float)sy + 1.2f, (float)sz + 2.0f);
        a->player.yaw = 180.0f;
        a->player.pitch = -5.0f;
    } else if (!strcmp(scene, "mobs")) {
        for (int dx = -6; dx <= 6; dx++) {
            for (int dz = 2; dz <= 14; dz++) {
                for (int dy = 0; dy <= 4; dy++)
                    world_set_block(&a->world, sx + dx, sy + dy, sz + dz, BLOCK_AIR, false);
                world_set_block(&a->world, sx + dx, sy - 1, sz + dz, BLOCK_STONE_BRICKS, false);
            }
        }
        a->player.pos = vec3((float)sx, (float)sy + 0.2f, (float)sz + 2.0f);
        a->player.yaw = 180.0f;
        a->player.pitch = -10.0f;
        a->player.hotbar[a->player.selected] = ITEM_MACE;
    }
    if (a->has_yaw) a->player.yaw = a->forced_yaw;
    if (a->has_pitch) a->player.pitch = a->forced_pitch;
}

static void start_world(App *a, bool new_world) {
    if (a->world_active) {
        save_world(a, false);
        world_destroy(&a->world);
        renderer_reset_chunks(&a->renderer);
        a->world_active = false;
    }

    a->has_save = access(a->save_path, R_OK) == 0;
    /* Screenshot/headless runs must never rotate or delete a real save. */
    if (new_world && a->has_save && !a->no_save && !a->headless) {
        char backup[700];
        snprintf(backup, sizeof(backup), "%s/world_backup.mced", a->save_dir);
        rename(a->save_path, backup);
        remove(a->player_path);
        a->has_save = false;
    }

    present_loading(a, "Reading world data...", 0.03f);

    uint64_t seed = 0;
    bool load_ok = false;
    if (!new_world && a->has_save) {
        bool hs = false;
        if (world_probe_seed(a->save_path, &seed, &hs) && hs)
            load_ok = true;
    }
    if (!load_ok)
        seed = make_seed(a);
    if (a->has_seed_override && new_world)
        seed = a->seed_override;

    world_init(&a->world, seed, a->renderer.render_radius + 2);
    if (a->pool) {
        world_set_thread_pool(&a->world, a->pool);
        renderer_set_thread_pool(&a->renderer, a->pool);
    }
    if (load_ok) {
        FILE *f = fopen(a->save_path, "rb");
        if (f) {
            if (!world_read(&a->world, f))
                fprintf(stderr, "save file could not be fully read; continuing with terrain\n");
            fclose(f);
        }
        a->world.dirty = false;
    }

    int sx, sy, sz;
    world_find_spawn(&a->world, &sx, &sy, &sz);
    player_init(&a->player, vec3((float)sx + 0.5f, (float)sy, (float)sz + 0.5f));
    a->player.yaw = 0;
    a->player.pitch = 0;
    if (a->has_yaw) a->player.yaw = a->forced_yaw;
    if (a->has_pitch) a->player.pitch = a->forced_pitch;
    if (load_ok)
        player_load(a);
    if (a->has_eye_override) {
        a->player.pos = vec3(a->eye_override[0], a->eye_override[1], a->eye_override[2]);
        a->player.flying = true;
        int px = (int)floorf(a->player.pos.x);
        int py = (int)floorf(a->player.pos.y + 0.1f);
        int pz = (int)floorf(a->player.pos.z);
        world_preload(&a->world, px, pz, 1);
        if (block_is_solid(world_get_block(&a->world, px, py, pz))) {
            int found_y = -1;
            for (int dy = 1; dy <= 25; dy++) {
                if (!block_is_solid(world_get_block(&a->world, px, py - dy, pz)) && py - dy >= 4) {
                    found_y = py - dy; break;
                }
                if (!block_is_solid(world_get_block(&a->world, px, py + dy, pz)) && py + dy < WORLD_HEIGHT - 4) {
                    found_y = py + dy; break;
                }
            }
            if (found_y > 0)
                a->player.pos.y = (float)found_y + 0.1f;
        }
    }
    if (a->has_yaw) a->player.yaw = a->forced_yaw;
    if (a->has_pitch) a->player.pitch = a->forced_pitch;
    a->player.dead = false;
    a->player.hurt_timer = 0;
    a->player.fall_distance = 0;
    a->player.regen_timer = 0;

    /* Keep the player out of terrain if a save was made inside a wall. */
    if (!a->has_eye_override && block_is_solid(world_get_block(&a->world,
            (int)floorf(a->player.pos.x), (int)floorf(a->player.pos.y + 0.1f),
            (int)floorf(a->player.pos.z)))) {
        a->player.pos = vec3((float)sx + 0.5f, (float)sy + 0.1f, (float)sz + 0.5f);
    }

    char status[128];
    int preload_r = a->renderer.render_radius;
    for (int r = 0; r <= preload_r; r++) {
        int pct = preload_r > 0 ? (r * 75) / preload_r : 75;
        snprintf(status, sizeof(status), "Generating terrain... %d%%", pct);
        present_loading(a, status, 0.05f + (float)pct * 0.01f);
        world_preload(&a->world, a->player.pos.x, a->player.pos.z, r);
    }
    present_loading(a, "Building chunk meshes...", 0.82f);
    renderer_warmup(&a->renderer, &a->world,
                    vec3(a->player.pos.x, a->player.pos.y + PLAYER_EYE, a->player.pos.z));
    present_loading(a, "Ready", 1.0f);

    a->world_active = true;
    if (!load_ok || a->world_time <= 0.0f)
        a->world_time = 300.0f;
    if (a->has_time_override)
        a->world_time = a->time_override;
    renderer_particles_clear(&a->renderer);
    a->active_tnt_count = 0;
    a->dropped_count = 0;
    entity_manager_init(&a->entities, &a->assets);
    if (a->scene_name) {
        setup_scene(a, a->scene_name);
    }
    a->save_timer = 0;
    a->cave_timer = 25;
    a->step_accum = 0;
    a->last_step_pos = a->player.pos;
    ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
    audio_music_menu(&a->audio, false);
    update_mouse_capture(a);
    set_toast(a, load_ok ? "World loaded" : "New world generated");
    printf("world: seed %llu, %d chunks, spawn %.1f %.1f %.1f\n",
           (unsigned long long)seed, a->world.chunk_count,
           a->player.pos.x, a->player.pos.y, a->player.pos.z);
}

static void apply_settings(App *a) {
    a->renderer.fov = (float)a->settings.fov;
    a->renderer.render_radius = a->settings.render_distance;
    if (a->world_active)
        a->world.load_radius = a->settings.render_distance + 2;
    a->renderer.clouds_enabled = a->settings.clouds;
    a->renderer.fog_enabled = a->settings.fog;
    a->renderer.brightness = a->settings.brightness;

    if (a->settings.gui_scale > 0) {
        a->renderer.ui_scale = (float)a->settings.gui_scale;
        a->renderer.ui_width = (int)((float)a->renderer.width / a->renderer.ui_scale);
        a->renderer.ui_height = (int)((float)a->renderer.height / a->renderer.ui_scale);
    } else {
        float scale = (float)a->renderer.height / 360.0f;
        if (scale < 1.0f) scale = 1.0f;
        a->renderer.ui_scale = scale;
        a->renderer.ui_width = (int)((float)a->renderer.width / scale);
        a->renderer.ui_height = (int)((float)a->renderer.height / scale);
    }
    a->ui.width = a->renderer.ui_width;
    a->ui.height = a->renderer.ui_height;

    if (a->window) {
        SDL_GL_SetSwapInterval(a->settings.vsync ? 1 : 0);
        SDL_SetWindowFullscreen(a->window,
                                a->settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }

    audio_set_enabled(&a->audio, !a->settings.mute && !a->no_audio);
    audio_set_master_volume(&a->audio, a->settings.master_volume);
    audio_set_music_volume(&a->audio, a->settings.music_volume);
    audio_set_sound_volume(&a->audio, a->settings.sound_volume);
    audio_set_blocks_volume(&a->audio, a->settings.blocks_volume);
}

static void respawn_player(App *a) {
    int sx, sy, sz;
    world_find_spawn(&a->world, &sx, &sy, &sz);
    player_respawn(&a->player, vec3((float)sx + 0.5f, (float)sy + 0.1f,
                                    (float)sz + 0.5f));
    a->player.yaw = 0;
    a->player.pitch = 0;
    renderer_particles_clear(&a->renderer);
    ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
    update_mouse_capture(a);
    set_toast(a, "Respawned at world spawn");
}

static void handle_action(App *a, UIAction action) {
    if (action != UI_ACT_NONE)
        audio_play_click(&a->audio);
    switch (action) {
    case UI_ACT_CONTINUE:
        start_world(a, false);
        break;
    case UI_ACT_NEW_WORLD:
        ui_set_screen(&a->ui, UI_SCREEN_CREATE_WORLD, a->has_save);
        break;
    case UI_ACT_CREATE_WORLD_CANCEL:
        ui_set_screen(&a->ui, UI_SCREEN_TITLE, a->has_save);
        break;
    case UI_ACT_CREATE_WORLD_START: {
        if (a->ui.world_seed_len > 0) {
            uint64_t h = 14695981039346656037ULL;
            for (int i = 0; i < a->ui.world_seed_len; i++) {
                h = (h ^ (uint8_t)a->ui.world_seed[i]) * 1099511628211ULL;
            }
            a->seed_override = h;
            a->has_seed_override = true;
        }
        a->player.creative = (a->ui.create_gamemode == 1);
        a->player.flying = a->player.creative;
        start_world(a, true);
        break;
    }
    case UI_ACT_QUIT:
        a->running = false;
        break;
    case UI_ACT_RESUME:
        ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
        update_mouse_capture(a);
        break;
    case UI_ACT_SAVE_QUIT:
        save_world(a, true);
        enter_title(a);
        break;
    case UI_ACT_INV_CLOSE:
        ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
        update_mouse_capture(a);
        break;
    case UI_ACT_INV_PREV:
        ui_scroll_inventory(&a->ui, -1);
        break;
    case UI_ACT_INV_NEXT:
        ui_scroll_inventory(&a->ui, 1);
        break;
    case UI_ACT_PICK_BLOCK: {
        int b = a->ui.clicked_block;
        if (b > 0 && b < BLOCK_COUNT) {
            a->player.hotbar[a->player.selected] = (uint8_t)b;
            char t[128];
            snprintf(t, sizeof(t), "%s -> slot %d", g_block_defs[b].name,
                     a->player.selected + 1);
            set_toast(a, t);
            audio_play_place(&a->audio, (uint8_t)b);
        }
        break;
    }
    case UI_ACT_RESPAWN:
        respawn_player(a);
        break;
    case UI_ACT_DEATH_TITLE:
        respawn_player(a);
        save_world(a, true);
        enter_title(a);
        break;
    case UI_ACT_OPEN_OPTIONS:
        a->ui.prev_screen = a->ui.screen;
        ui_set_screen(&a->ui, UI_SCREEN_OPTIONS, a->has_save);
        break;
    case UI_ACT_OPEN_VIDEO:
        ui_set_screen(&a->ui, UI_SCREEN_VIDEO, a->has_save);
        break;
    case UI_ACT_OPEN_AUDIO:
        ui_set_screen(&a->ui, UI_SCREEN_AUDIO, a->has_save);
        break;
    case UI_ACT_OPEN_CONTROLS:
        ui_set_screen(&a->ui, UI_SCREEN_CONTROLS, a->has_save);
        break;
    case UI_ACT_OPEN_ADVANCEMENTS:
        ui_set_screen(&a->ui, UI_SCREEN_ADVANCEMENTS, a->has_save);
        break;
    case UI_ACT_DONE:
        settings_save(&a->settings, a->options_path);
        apply_settings(a);
        if (a->ui.screen == UI_SCREEN_OPTIONS) {
            ui_set_screen(&a->ui, a->ui.prev_screen ? a->ui.prev_screen : (a->world_active ? UI_SCREEN_PAUSE : UI_SCREEN_TITLE), a->has_save);
        } else if (a->ui.screen == UI_SCREEN_ADVANCEMENTS) {
            ui_set_screen(&a->ui, UI_SCREEN_PAUSE, a->has_save);
        } else {
            ui_set_screen(&a->ui, UI_SCREEN_OPTIONS, a->has_save);
        }
        break;
    case UI_ACT_TOGGLE_VSYNC:
        a->settings.vsync = !a->settings.vsync;
        ui_refresh_button_labels(&a->ui);
        apply_settings(a);
        break;
    case UI_ACT_TOGGLE_BOBBING:
        a->settings.view_bobbing = !a->settings.view_bobbing;
        ui_refresh_button_labels(&a->ui);
        break;
    case UI_ACT_TOGGLE_CLOUDS:
        a->settings.clouds = !a->settings.clouds;
        ui_refresh_button_labels(&a->ui);
        apply_settings(a);
        break;
    case UI_ACT_TOGGLE_FOG:
        a->settings.fog = !a->settings.fog;
        ui_refresh_button_labels(&a->ui);
        apply_settings(a);
        break;
    case UI_ACT_TOGGLE_FULLSCREEN:
        a->settings.fullscreen = !a->settings.fullscreen;
        ui_refresh_button_labels(&a->ui);
        apply_settings(a);
        break;
    case UI_ACT_TOGGLE_MUTE:
        a->settings.mute = !a->settings.mute;
        ui_refresh_button_labels(&a->ui);
        apply_settings(a);
        break;
    case UI_ACT_TOGGLE_INVERT_MOUSE:
        a->settings.invert_mouse = !a->settings.invert_mouse;
        ui_refresh_button_labels(&a->ui);
        break;
    case UI_ACT_CYCLE_GUI_SCALE:
        a->settings.gui_scale = (a->settings.gui_scale + 1) % 4;
        apply_settings(a);
        ui_set_screen(&a->ui, a->ui.screen, a->has_save);
        break;
    default:
        break;
    }
}

static void toggle_fullscreen(App *a) {
    if (a->headless)
        return;
    a->fullscreen = !a->fullscreen;
    SDL_SetWindowFullscreen(a->window,
                            a->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

/* SDL_RenderSetLogicalSize() already filters mouse events into the
   renderer's logical resolution, so event coordinates can be passed to
   the UI directly.  SDL_RenderWindowToLogical() must not be applied again:
   doing so scales the values a second time and misses every button. */
static void click_at(App *a, int lx, int ly) {
    float x = (float)lx;
    float y = (float)ly;
#if MCED_USE_GL
    if (a->renderer.ui_scale > 0.0f) {
        x /= a->renderer.ui_scale;
        y /= a->renderer.ui_scale;
    }
#endif
    UIAction action = ui_click(&a->ui, x, y, &a->player);
    if (action != UI_ACT_NONE)
        handle_action(a, action);
}

static void screenshot_now(App *a) {
    char path[768];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    snprintf(path, sizeof(path), "%s/mced_%04d%02d%02d_%02d%02d%02d.png",
             a->screenshot_dir, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    renderer_screenshot(&a->renderer, path);
    set_toast(a, "Screenshot saved");
}

static void execute_chat_command(App *a, const char *raw_line) {
    if (!raw_line || !raw_line[0])
        return;

    const char *line = raw_line;
    while (*line == ' ') line++;
    if (!*line) return;

    if (line[0] != '/') {
        char msg[160];
        snprintf(msg, sizeof(msg), "<Player> %s", line);
        ui_chat_add(&a->ui, msg, (Color3){1.0f, 1.0f, 1.0f});
        return;
    }

    char cmd[32] = {0};
    char arg1[32] = {0};
    char arg2[32] = {0};
    char arg3[32] = {0};
    int n = sscanf(line + 1, "%31s %31s %31s %31s", cmd, arg1, arg2, arg3);
    if (n < 1) return;

    if (strcmp(cmd, "gamemode") == 0 || strcmp(cmd, "gm") == 0) {
        if (strcmp(arg1, "creative") == 0 || strcmp(arg1, "c") == 0 || strcmp(arg1, "1") == 0) {
            a->player.creative = true;
            a->player.flying = true;
            a->player.health = PLAYER_MAX_HEALTH;
            a->player.air = PLAYER_MAX_AIR;
            set_toast(a, "Game mode: Creative");
            ui_chat_add(&a->ui, "Set own game mode to Creative Mode", (Color3){1.0f, 1.0f, 0.35f});
        } else if (strcmp(arg1, "survival") == 0 || strcmp(arg1, "s") == 0 || strcmp(arg1, "0") == 0) {
            a->player.creative = false;
            a->player.flying = false;
            set_toast(a, "Game mode: Survival");
            ui_chat_add(&a->ui, "Set own game mode to Survival Mode", (Color3){1.0f, 1.0f, 0.35f});
        } else {
            ui_chat_add(&a->ui, "Usage: /gamemode <creative|survival>", (Color3){1.0f, 0.35f, 0.35f});
        }
    } else if (strcmp(cmd, "gmc") == 0) {
        a->player.creative = true;
        a->player.flying = true;
        a->player.health = PLAYER_MAX_HEALTH;
        a->player.air = PLAYER_MAX_AIR;
        set_toast(a, "Game mode: Creative");
        ui_chat_add(&a->ui, "Set own game mode to Creative Mode", (Color3){1.0f, 1.0f, 0.35f});
    } else if (strcmp(cmd, "gms") == 0) {
        a->player.creative = false;
        a->player.flying = false;
        set_toast(a, "Game mode: Survival");
        ui_chat_add(&a->ui, "Set own game mode to Survival Mode", (Color3){1.0f, 1.0f, 0.35f});
    } else if (strcmp(cmd, "time") == 0) {
        if (strcmp(arg1, "set") == 0) {
            if (strcmp(arg2, "day") == 0 || strcmp(arg2, "1000") == 0) {
                a->world_time = 50.0f;
                ui_chat_add(&a->ui, "Set the time to day", (Color3){1.0f, 1.0f, 0.35f});
            } else if (strcmp(arg2, "noon") == 0 || strcmp(arg2, "6000") == 0) {
                a->world_time = 300.0f;
                ui_chat_add(&a->ui, "Set the time to noon", (Color3){1.0f, 1.0f, 0.35f});
            } else if (strcmp(arg2, "night") == 0 || strcmp(arg2, "13000") == 0) {
                a->world_time = 650.0f;
                ui_chat_add(&a->ui, "Set the time to night", (Color3){1.0f, 1.0f, 0.35f});
            } else if (strcmp(arg2, "midnight") == 0 || strcmp(arg2, "18000") == 0) {
                a->world_time = 900.0f;
                ui_chat_add(&a->ui, "Set the time to midnight", (Color3){1.0f, 1.0f, 0.35f});
            } else {
                int val = atoi(arg2);
                a->world_time = ((float)val / 20.0f);
                char m[64];
                snprintf(m, sizeof(m), "Set the time to %d", val);
                ui_chat_add(&a->ui, m, (Color3){1.0f, 1.0f, 0.35f});
            }
        } else {
            ui_chat_add(&a->ui, "Usage: /time set <day|night|noon|midnight|ticks>", (Color3){1.0f, 0.35f, 0.35f});
        }
    } else if (strcmp(cmd, "day") == 0) {
        a->world_time = 300.0f;
        ui_chat_add(&a->ui, "Set the time to day", (Color3){1.0f, 1.0f, 0.35f});
    } else if (strcmp(cmd, "night") == 0) {
        a->world_time = 900.0f;
        ui_chat_add(&a->ui, "Set the time to night", (Color3){1.0f, 1.0f, 0.35f});
    } else if (strcmp(cmd, "clear") == 0) {
        for (int i = 0; i < HOTBAR_SLOTS; i++)
            a->player.hotbar[i] = BLOCK_AIR;
        ui_chat_add(&a->ui, "Cleared the inventory", (Color3){1.0f, 1.0f, 0.35f});
    } else if (strcmp(cmd, "give") == 0) {
        uint8_t bid = BLOCK_AIR;
        if (isdigit((unsigned char)arg1[0])) {
            int val = atoi(arg1);
            if (val > 0 && val < BLOCK_COUNT) bid = (uint8_t)val;
        } else {
            for (int b = 1; b < BLOCK_COUNT; b++) {
                const char *bname = g_block_defs[b].name;
                if (bname && (strcasecmp(arg1, bname) == 0 ||
                              strstr(bname, arg1) != NULL)) {
                    bid = (uint8_t)b;
                    break;
                }
            }
        }
        if (bid > 0 && bid < BLOCK_COUNT) {
            a->player.hotbar[a->player.selected] = bid;
            char msg[96];
            snprintf(msg, sizeof(msg), "Gave [%s] to Player",
                     g_block_defs[bid].name ? g_block_defs[bid].name : "Block");
            ui_chat_add(&a->ui, msg, (Color3){1.0f, 1.0f, 0.35f});
        } else {
            ui_chat_add(&a->ui, "Unknown block name or ID", (Color3){1.0f, 0.35f, 0.35f});
        }
    } else if (strcmp(cmd, "tp") == 0 || strcmp(cmd, "teleport") == 0) {
        if (n >= 4) {
            float x = (float)atof(arg1);
            float y = (float)atof(arg2);
            float z = (float)atof(arg3);
            a->player.pos = vec3(x, y, z);
            a->player.vel = vec3(0, 0, 0);
            char msg[96];
            snprintf(msg, sizeof(msg), "Teleported Player to %.1f, %.1f, %.1f", x, y, z);
            ui_chat_add(&a->ui, msg, (Color3){1.0f, 1.0f, 0.35f});
        } else {
            ui_chat_add(&a->ui, "Usage: /tp <x> <y> <z>", (Color3){1.0f, 0.35f, 0.35f});
        }
    } else if (strcmp(cmd, "spawn") == 0) {
        int sx, sy, sz;
        world_find_spawn(&a->world, &sx, &sy, &sz);
        a->player.pos = vec3((float)sx + 0.5f, (float)sy + 0.1f, (float)sz + 0.5f);
        a->player.vel = vec3(0, 0, 0);
        ui_chat_add(&a->ui, "Teleported Player to world spawn", (Color3){1.0f, 1.0f, 0.35f});
    } else if (strcmp(cmd, "kill") == 0) {
        a->player.health = 0;
        a->player.dead = true;
        ui_chat_add(&a->ui, "Ouch! That looks like it hurt", (Color3){1.0f, 0.35f, 0.35f});
    } else if (strcmp(cmd, "rd") == 0 || strcmp(cmd, "renderdistance") == 0) {
        if (n >= 2) {
            int dist = atoi(arg1);
            if (dist < 2) dist = 2;
            if (dist > 32) dist = 32;
            a->renderer.render_radius = dist;
            a->world.load_radius = dist + 2;
            char msg[96];
            snprintf(msg, sizeof(msg), "Render distance set to %d chunks (%d blocks)", dist, dist * CHUNK_SIZE);
            ui_chat_add(&a->ui, msg, (Color3){1.0f, 1.0f, 0.35f});
            set_toast(a, msg);
        } else {
            char msg[96];
            snprintf(msg, sizeof(msg), "Render distance: %d chunks (%dm). Usage: /rd <2-32>",
                     a->renderer.render_radius, a->renderer.render_radius * CHUNK_SIZE);
            ui_chat_add(&a->ui, msg, (Color3){1.0f, 1.0f, 0.35f});
        }
    } else if (strcmp(cmd, "summon") == 0 || strcmp(cmd, "spawnmob") == 0) {
        if (n >= 2 && strcasecmp(arg1, "arrow") == 0) {
            Vec3 spawn_pos = vec3(a->player.pos.x + 2.0f, a->player.pos.y, a->player.pos.z + 2.0f);
            entity_spawn(&a->entities, ENTITY_ARROW, spawn_pos);
            ui_chat_add(&a->ui, "Summoned Arrow", (Color3){1.0f, 1.0f, 0.35f});
        } else {
            ui_chat_add(&a->ui, "Mobs are disabled", (Color3){1.0f, 0.4f, 0.4f});
        }
    } else if (strcmp(cmd, "help") == 0) {
        ui_chat_add(&a->ui, "--- Commands Help ---", (Color3){0.4f, 1.0f, 0.4f});
        ui_chat_add(&a->ui, "/gamemode <creative|survival>", (Color3){0.9f, 0.9f, 0.9f});
        ui_chat_add(&a->ui, "/time set <day|night|noon|ticks>", (Color3){0.9f, 0.9f, 0.9f});
        ui_chat_add(&a->ui, "/tp <x> <y> <z>  or  /spawn", (Color3){0.9f, 0.9f, 0.9f});
        ui_chat_add(&a->ui, "/give <block>    or  /clear", (Color3){0.9f, 0.9f, 0.9f});
        ui_chat_add(&a->ui, "/rd <2-32>       or  /renderdistance", (Color3){0.9f, 0.9f, 0.9f});
        ui_chat_add(&a->ui, "/kill", (Color3){0.9f, 0.9f, 0.9f});
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "Unknown command: /%s. Type /help for help", cmd);
        ui_chat_add(&a->ui, msg, (Color3){1.0f, 0.35f, 0.35f});
    }
}

static void refund_item_or_drop(App *a, uint8_t item, uint8_t count) {
    if (item == BLOCK_AIR || count == 0) return;
    for (int i = 0; i < HOTBAR_SLOTS && count > 0; i++) {
        if (a->player.hotbar[i] == item && a->player.hotbar_counts[i] < 64) {
            int space = 64 - a->player.hotbar_counts[i];
            int take = (count < space) ? count : space;
            a->player.hotbar_counts[i] += (uint8_t)take;
            count -= (uint8_t)take;
        }
    }
    for (int i = 0; i < INVENTORY_SLOTS && count > 0; i++) {
        if (a->player.inventory[i] == item && a->player.inventory_counts[i] < 64) {
            int space = 64 - a->player.inventory_counts[i];
            int take = (count < space) ? count : space;
            a->player.inventory_counts[i] += (uint8_t)take;
            count -= (uint8_t)take;
        }
    }
    for (int i = 0; i < HOTBAR_SLOTS && count > 0; i++) {
        if (a->player.hotbar[i] == BLOCK_AIR) {
            a->player.hotbar[i] = item;
            int take = (count < 64) ? count : 64;
            a->player.hotbar_counts[i] = (uint8_t)take;
            count -= (uint8_t)take;
        }
    }
    for (int i = 0; i < INVENTORY_SLOTS && count > 0; i++) {
        if (a->player.inventory[i] == BLOCK_AIR) {
            a->player.inventory[i] = item;
            int take = (count < 64) ? count : 64;
            a->player.inventory_counts[i] = (uint8_t)take;
            count -= (uint8_t)take;
        }
    }
    if (count > 0) {
        int bx = (int)floorf(a->player.pos.x);
        int by = (int)floorf(a->player.pos.y + 0.5f);
        int bz = (int)floorf(a->player.pos.z);
        spawn_dropped_item(a, item, bx, by, bz);
    }
}

static void refund_crafting_items(App *a) {
    if (a->player.cursor_item != BLOCK_AIR && a->player.cursor_count > 0) {
        refund_item_or_drop(a, a->player.cursor_item, a->player.cursor_count);
        a->player.cursor_item = BLOCK_AIR;
        a->player.cursor_count = 0;
    }
    for (int i = 0; i < 4; i++) {
        if (a->player.crafting[i] != BLOCK_AIR && a->player.crafting_counts[i] > 0) {
            refund_item_or_drop(a, a->player.crafting[i], a->player.crafting_counts[i]);
            a->player.crafting[i] = BLOCK_AIR;
            a->player.crafting_counts[i] = 0;
        }
    }
    player_update_crafting(&a->player);
    for (int i = 0; i < 9; i++) {
        if (a->player.crafting_3x3[i] != BLOCK_AIR && a->player.crafting_3x3_counts[i] > 0) {
            refund_item_or_drop(a, a->player.crafting_3x3[i], a->player.crafting_3x3_counts[i]);
            a->player.crafting_3x3[i] = BLOCK_AIR;
            a->player.crafting_3x3_counts[i] = 0;
        }
    }
    player_update_crafting_3x3(&a->player);
}

static void handle_key(App *a, SDL_Keycode key, bool down) {
    if (!down)
        return;

    if (a->ui.screen == UI_SCREEN_CHAT) {
        if (key == SDLK_ESCAPE) {
            ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
            SDL_StopTextInput();
            update_mouse_capture(a);
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            execute_chat_command(a, a->ui.chat_input);
            ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
            SDL_StopTextInput();
            update_mouse_capture(a);
        } else if (key == SDLK_BACKSPACE) {
            ui_chat_backspace(&a->ui);
        }
        return;
    }

    if (a->ui.screen == UI_SCREEN_CREATE_WORLD) {
        if (key == SDLK_ESCAPE) {
            ui_set_screen(&a->ui, UI_SCREEN_TITLE, a->has_save);
        } else if (key == SDLK_BACKSPACE) {
            ui_text_input_backspace(&a->ui);
        } else if (key == SDLK_TAB) {
            a->ui.focused_text_field = (a->ui.focused_text_field == 1) ? 2 : 1;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            handle_action(a, UI_ACT_CREATE_WORLD_START);
        }
        return;
    }

    switch (key) {
    case SDLK_ESCAPE:
        if (a->ui.screen == UI_SCREEN_NONE) {
            ui_set_screen(&a->ui, UI_SCREEN_PAUSE, a->has_save);
            update_mouse_capture(a);
        } else if (a->ui.screen == UI_SCREEN_PAUSE) {
            ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
            update_mouse_capture(a);
        } else if (a->ui.screen == UI_SCREEN_INVENTORY ||
                   a->ui.screen == UI_SCREEN_CRAFTING ||
                   a->ui.screen == UI_SCREEN_FURNACE ||
                   a->ui.screen == UI_SCREEN_CHEST) {
            if (a->ui.screen == UI_SCREEN_CHEST) {
                audio_play_chest_close(&a->audio);
            }
            refund_crafting_items(a);
            ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
            update_mouse_capture(a);
        } else if (a->ui.screen == UI_SCREEN_OPTIONS) {
            settings_save(&a->settings, a->options_path);
            apply_settings(a);
            ui_set_screen(&a->ui, a->ui.prev_screen ? a->ui.prev_screen : (a->world_active ? UI_SCREEN_PAUSE : UI_SCREEN_TITLE), a->has_save);
            if (a->ui.screen == UI_SCREEN_NONE) update_mouse_capture(a);
        } else if (a->ui.screen == UI_SCREEN_VIDEO || a->ui.screen == UI_SCREEN_AUDIO || a->ui.screen == UI_SCREEN_CONTROLS) {
            settings_save(&a->settings, a->options_path);
            apply_settings(a);
            ui_set_screen(&a->ui, UI_SCREEN_OPTIONS, a->has_save);
        } else if (a->ui.screen == UI_SCREEN_ADVANCEMENTS) {
            ui_set_screen(&a->ui, UI_SCREEN_PAUSE, a->has_save);
        }
        break;
    case SDLK_t:
        if (a->world_active && a->ui.screen == UI_SCREEN_NONE) {
            ui_chat_open(&a->ui, "");
            a->ignore_slash_text = false;
            SDL_StartTextInput();
            update_mouse_capture(a);
        }
        break;
    case SDLK_SLASH:
        if (a->world_active && a->ui.screen == UI_SCREEN_NONE) {
            ui_chat_open(&a->ui, "/");
            a->ignore_slash_text = true;
            SDL_StartTextInput();
            update_mouse_capture(a);
        }
        break;
    case SDLK_e:
        if (!a->world_active)
            break;
        if (a->ui.screen == UI_SCREEN_NONE) {
            ui_set_screen(&a->ui, UI_SCREEN_INVENTORY, a->has_save);
            update_mouse_capture(a);
            audio_play_click(&a->audio);
        } else if (a->ui.screen == UI_SCREEN_INVENTORY ||
                   a->ui.screen == UI_SCREEN_CRAFTING ||
                   a->ui.screen == UI_SCREEN_FURNACE ||
                   a->ui.screen == UI_SCREEN_CHEST) {
            if (a->ui.screen == UI_SCREEN_CHEST) {
                audio_play_chest_close(&a->audio);
            }
            refund_crafting_items(a);
            ui_set_screen(&a->ui, UI_SCREEN_NONE, a->has_save);
            update_mouse_capture(a);
            audio_play_click(&a->audio);
        }
        break;
    case SDLK_F3:
        a->ui.show_debug = !a->ui.show_debug;
        break;
    case SDLK_F2:
        screenshot_now(a);
        break;
    case SDLK_F11:
        toggle_fullscreen(a);
        break;
    case SDLK_f:
        if (a->world_active && a->ui.screen == UI_SCREEN_NONE) {
            a->player.flying = !a->player.flying;
            a->player.vel.y = 0;
            set_toast(a, a->player.flying ? "Flying enabled" : "Flying disabled");
        }
        break;
    case SDLK_F4:
        if (a->world_active && a->ui.screen == UI_SCREEN_NONE) {
            a->player.creative = !a->player.creative;
            a->player.flying = a->player.creative;
            set_toast(a, a->player.creative ? "Game mode: Creative" : "Game mode: Survival");
            audio_play_pop(&a->audio);
        }
        break;
    case SDLK_m:
        a->audio.enabled = !a->audio.enabled;
        audio_set_enabled(&a->audio, a->audio.enabled);
        set_toast(a, a->audio.enabled ? "Sound on" : "Sound off");
        break;
    default:
        if (a->world_active && a->ui.screen == UI_SCREEN_NONE &&
            key >= SDLK_1 && key <= SDLK_9) {
            a->player.selected = (int)(key - SDLK_1);
        }
        break;
    }
}

static void handle_event(App *a, SDL_Event *e) {
    switch (e->type) {
    case SDL_QUIT:
        a->running = false;
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_RESIZED ||
            e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            a->win_w = e->window.data1;
            a->win_h = e->window.data2;
#if MCED_USE_GL
            int dw = a->win_w, dh = a->win_h;
            SDL_GL_GetDrawableSize(a->window, &dw, &dh);
            a->render_w = dw;
            a->render_h = dh;
            renderer_resize(&a->renderer, dw, dh);
            ui_init(&a->ui, a->renderer.ui_width, a->renderer.ui_height);
            ui_set_screen(&a->ui, a->ui.screen, a->has_save);
#endif
        }
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        handle_key(a, e->key.keysym.sym, e->type == SDL_KEYDOWN);
        break;
    case SDL_TEXTINPUT:
        if (a->ui.screen == UI_SCREEN_CHAT) {
            for (int i = 0; e->text.text[i] != '\0'; i++) {
                char ch = e->text.text[i];
                if (ch == '/' && a->ignore_slash_text) {
                    a->ignore_slash_text = false;
                    continue;
                }
                a->ignore_slash_text = false;
                ui_chat_input_char(&a->ui, ch);
            }
        } else if (a->ui.screen == UI_SCREEN_CREATE_WORLD) {
            for (int i = 0; e->text.text[i] != '\0'; i++) {
                ui_text_input_char(&a->ui, e->text.text[i]);
            }
        }
        break;
    case SDL_MOUSEMOTION:
        if (a->world_active && a->ui.screen == UI_SCREEN_NONE) {
            float sens = 0.12f * a->settings.mouse_sensitivity;
            float pitch_mult = a->settings.invert_mouse ? 1.0f : -1.0f;
            int dx, dy;
            int bound_w = a->win_w > 0 ? a->win_w : a->internal_w;
            int bound_h = a->win_h > 0 ? a->win_h : a->internal_h;
            if (a->soft_look) {
                int mx = e->motion.x, my = e->motion.y;
                if (!a->look_anchor) {
                    a->look_last_x = mx;
                    a->look_last_y = my;
                    a->look_anchor = true;
                    break;
                }
                if (a->look_warp_pending) {
                    int cx = bound_w / 2, cy = bound_h / 2;
                    /* A recenter can produce a small burst of intermediate
                       motion events under XWayland.  Ignore the whole burst
                       until the cursor actually reaches the new anchor. */
                    if (abs(mx - cx) <= 4 && abs(my - cy) <= 4) {
                        a->look_warp_pending = false;
                        a->look_last_x = mx;
                        a->look_last_y = my;
                        break;
                    }
                    if (SDL_GetTicks() - a->look_warp_ticks < 150)
                        break;
                    a->look_warp_pending = false;
                    a->look_last_x = mx;
                    a->look_last_y = my;
                    break;
                }
                dx = mx - a->look_last_x;
                dy = my - a->look_last_y;
                /* A lost/gained focus jump must not spin the view. */
                if (abs(dx) > bound_w || abs(dy) > bound_h) {
                    a->look_last_x = mx;
                    a->look_last_y = my;
                    break;
                }
                a->look_last_x = mx;
                a->look_last_y = my;
            } else {
                dx = e->motion.xrel;
                dy = e->motion.yrel;
            }
            if (dx || dy) {
                a->player.yaw -= (float)dx * sens;
                a->player.pitch += (float)dy * sens * pitch_mult;
                a->player.pitch = clampf(a->player.pitch, -89.0f, 89.0f);
                while (a->player.yaw < 0) a->player.yaw += 360.0f;
                while (a->player.yaw >= 360.0f) a->player.yaw -= 360.0f;
            }
            if (a->soft_look) {
                const int margin = 24;
                int mx = a->look_last_x, my = a->look_last_y;
                if (mx < margin || mx >= bound_w - margin ||
                    my < margin || my >= bound_h - margin) {
                    int ww = 0, wh = 0;
                    SDL_GetWindowSize(a->window, &ww, &wh);
                    SDL_WarpMouseInWindow(a->window, ww / 2, wh / 2);
                    a->look_last_x = bound_w / 2;
                    a->look_last_y = bound_h / 2;
                    a->look_warp_pending = true;
                    a->look_warp_ticks = SDL_GetTicks();
                }
            }
        } else {
            float mx = (float)e->motion.x;
            float my = (float)e->motion.y;
#if MCED_USE_GL
            if (a->renderer.ui_scale > 0.0f) {
                mx /= a->renderer.ui_scale;
                my /= a->renderer.ui_scale;
            }
#endif
            ui_mouse_move(&a->ui, mx, my);
            if (a->ui.active_slider >= 0) {
                apply_settings(a);
            }
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            if (a->ui.screen == UI_SCREEN_NONE && a->world_active)
                a->mouse_left = true;
            else if (a->ui.screen == UI_SCREEN_INVENTORY) {
                bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                ui_slot_click_event(&a->ui, &a->player, false, shift);
                audio_play_click(&a->audio);
            } else if (a->ui.screen != UI_SCREEN_LOADING) {
                float mx = (float)e->button.x;
                float my = (float)e->button.y;
#if MCED_USE_GL
                if (a->renderer.ui_scale > 0.0f) {
                    mx /= a->renderer.ui_scale;
                    my /= a->renderer.ui_scale;
                }
#endif
                if (ui_mouse_down(&a->ui, mx, my)) {
                    apply_settings(a);
                } else {
                    click_at(a, e->button.x, e->button.y);
                }
            }
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            if (a->ui.screen == UI_SCREEN_NONE && a->world_active)
                a->mouse_right = true;
            else if (a->ui.screen == UI_SCREEN_INVENTORY) {
                ui_slot_click_event(&a->ui, &a->player, true, false);
                audio_play_click(&a->audio);
            }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT) {
            a->mouse_left = false;
            if (a->ui.active_slider >= 0) {
                ui_mouse_up(&a->ui);
                apply_settings(a);
                settings_save(&a->settings, a->options_path);
            }
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            a->mouse_right = false;
        }
        break;
    case SDL_MOUSEWHEEL:
        if (a->ui.screen == UI_SCREEN_INVENTORY) {
            ui_scroll_inventory(&a->ui, -e->wheel.y);
        } else if (a->ui.screen == UI_SCREEN_NONE && a->world_active) {
            int dir = e->wheel.y > 0 ? -1 : 1;
            a->player.selected = (a->player.selected + dir + HOTBAR_SLOTS) % HOTBAR_SLOTS;
        }
        break;
    default:
        break;
    }
}

static void spawn_dropped_item_vec(App *a, uint8_t block, Vec3 pos) {
    if (block == BLOCK_AIR || block == BLOCK_BEDROCK)
        return;
    int slot = -1;
    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        if (!a->dropped_items[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        slot = 0;
    a->dropped_items[slot].active = true;
    a->dropped_items[slot].block = block;
    a->dropped_items[slot].count = 1;
    a->dropped_items[slot].pos = pos;
    float rx = ((float)(rand() % 100) / 50.0f - 1.0f) * 1.5f;
    float rz = ((float)(rand() % 100) / 50.0f - 1.0f) * 1.5f;
    a->dropped_items[slot].vel = vec3(rx, 3.5f + (float)(rand() % 100) / 100.0f, rz);
    a->dropped_items[slot].age = 0.0f;
    a->dropped_items[slot].rot = (float)(rand() % 360);
    if (slot >= a->dropped_count)
        a->dropped_count = slot + 1;
}

static void spawn_dropped_item(App *a, uint8_t block, int bx, int by, int bz) {
    spawn_dropped_item_vec(a, block, vec3((float)bx + 0.5f, (float)by + 0.35f, (float)bz + 0.5f));
}

static bool block_needs_support(uint8_t id) {
    switch (id) {
    case BLOCK_TORCH:
    case BLOCK_REDSTONE_TORCH:
    case BLOCK_REDSTONE_WIRE:
    case BLOCK_LEVER:
    case BLOCK_STONE_BUTTON:
    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_SHORT_GRASS:
    case BLOCK_CORNFLOWER:
    case BLOCK_ALLIUM:
    case BLOCK_AZURE_BLUET:
    case BLOCK_RED_TULIP:
    case BLOCK_ORANGE_TULIP:
    case BLOCK_WHITE_TULIP:
    case BLOCK_PINK_TULIP:
    case BLOCK_OXEYE_DAISY:
    case BLOCK_FERN:
    case BLOCK_SEAGRASS:
    case BLOCK_KELP:
    case BLOCK_TUBE_CORAL:
    case BLOCK_BRAIN_CORAL:
    case BLOCK_BUBBLE_CORAL:
    case BLOCK_FIRE_CORAL:
    case BLOCK_HORN_CORAL:
    case BLOCK_DEAD_BUSH:
    case BLOCK_SUGAR_CANE:
    case BLOCK_CACTUS:
    case BLOCK_OAK_DOOR:
    case BLOCK_OAK_DOOR_OPEN:
    case BLOCK_IRON_DOOR:
    case BLOCK_IRON_DOOR_OPEN:
        return true;
    default:
        return false;
    }
}

static bool block_support_valid(World *w, uint8_t id, int x, int y, int z) {
    if (y <= 0) return false;
    uint8_t below = world_get_block(w, x, y - 1, z);
    if (below == BLOCK_AIR || block_is_liquid(below)) return false;

    switch (id) {
    case BLOCK_TORCH:
    case BLOCK_REDSTONE_TORCH:
    case BLOCK_REDSTONE_WIRE:
    case BLOCK_LEVER:
    case BLOCK_STONE_BUTTON:
    case BLOCK_OAK_DOOR:
    case BLOCK_OAK_DOOR_OPEN:
    case BLOCK_IRON_DOOR:
    case BLOCK_IRON_DOOR_OPEN:
        return g_block_defs[below].solid;

    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_SHORT_GRASS:
    case BLOCK_CORNFLOWER:
    case BLOCK_ALLIUM:
    case BLOCK_AZURE_BLUET:
    case BLOCK_RED_TULIP:
    case BLOCK_ORANGE_TULIP:
    case BLOCK_WHITE_TULIP:
    case BLOCK_PINK_TULIP:
    case BLOCK_OXEYE_DAISY:
    case BLOCK_FERN:
        return below == BLOCK_GRASS_BLOCK || below == BLOCK_DIRT || below == BLOCK_COARSE_DIRT ||
               below == BLOCK_PODZOL || below == BLOCK_MOSS_BLOCK || below == BLOCK_PALE_MOSS_BLOCK;

    case BLOCK_SEAGRASS:
        return below == BLOCK_SAND || below == BLOCK_GRAVEL || below == BLOCK_DIRT ||
               below == BLOCK_CLAY || below == BLOCK_MUD;

    case BLOCK_KELP:
        return below == BLOCK_KELP || below == BLOCK_SAND || below == BLOCK_GRAVEL ||
               below == BLOCK_DIRT || below == BLOCK_CLAY;

    case BLOCK_TUBE_CORAL:
    case BLOCK_BRAIN_CORAL:
    case BLOCK_BUBBLE_CORAL:
    case BLOCK_FIRE_CORAL:
    case BLOCK_HORN_CORAL:
        return below == BLOCK_TUBE_CORAL_BLOCK || below == BLOCK_BRAIN_CORAL_BLOCK ||
               below == BLOCK_BUBBLE_CORAL_BLOCK || below == BLOCK_FIRE_CORAL_BLOCK ||
               below == BLOCK_HORN_CORAL_BLOCK || below == BLOCK_SAND || below == BLOCK_GRAVEL;

    case BLOCK_DEAD_BUSH:
        return below == BLOCK_SAND || below == BLOCK_RED_SAND || below == BLOCK_DIRT || below == BLOCK_COARSE_DIRT;

    case BLOCK_SUGAR_CANE:
        return below == BLOCK_SUGAR_CANE || below == BLOCK_GRASS_BLOCK || below == BLOCK_DIRT || below == BLOCK_SAND || below == BLOCK_RED_SAND;

    case BLOCK_CACTUS:
        return below == BLOCK_CACTUS || below == BLOCK_SAND || below == BLOCK_RED_SAND;

    default:
        return true;
    }
}

static void check_attached_block(World *w, App *a, int x, int y, int z) {
    if (y <= 0 || y >= WORLD_HEIGHT) return;
    uint8_t id = world_get_block(w, x, y, z);
    if (!block_needs_support(id)) return;

    if (!block_support_valid(w, id, x, y, z)) {
        world_set_block(w, x, y, z, BLOCK_AIR, true);
        uint8_t drop = block_drop_item(id, BLOCK_AIR);
        if (drop != BLOCK_AIR) {
            spawn_dropped_item(a, drop, x, y, z);
        }
        renderer_particles_break(&a->renderer, id, x, y, z);
        audio_play_break(&a->audio, id);
        check_attached_block(w, a, x, y + 1, z);
    }
}

static void check_falling_block(World *w, App *a, int x, int y, int z) {
    if (y <= 0 || y >= WORLD_HEIGHT) return;
    uint8_t id = world_get_block(w, x, y, z);
    if (id != BLOCK_SAND && id != BLOCK_RED_SAND && id != BLOCK_GRAVEL)
        return;

    int cur_y = y;
    while (cur_y > 0) {
        uint8_t below = world_get_block(w, x, cur_y - 1, z);
        if (below == BLOCK_AIR || block_is_liquid(below)) {
            world_set_block(w, x, cur_y, z, BLOCK_AIR, true);
            cur_y--;
            world_set_block(w, x, cur_y, z, id, true);
        } else if (below == BLOCK_TORCH) {
            world_set_block(w, x, cur_y, z, BLOCK_AIR, true);
            spawn_dropped_item(a, id, x, cur_y, z);
            break;
        } else {
            break;
        }
    }
}

static void trigger_falling_blocks(World *w, App *a, int x, int y, int z) {
    check_attached_block(w, a, x, y, z);
    for (int dy = 0; dy <= 32 && y + dy < WORLD_HEIGHT; dy++) {
        check_falling_block(w, a, x, y + dy, z);
    }
}

static void check_water_lava_at(World *w, App *a, int x, int y, int z) {
    uint8_t self = world_get_block(w, x, y, z);
    if (self != BLOCK_LAVA && self != BLOCK_WATER) return;

    int dx[6] = {1, -1, 0, 0, 0, 0};
    int dy[6] = {0, 0, 1, -1, 0, 0};
    int dz[6] = {0, 0, 0, 0, 1, -1};

    for (int i = 0; i < 6; i++) {
        int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= WORLD_HEIGHT) continue;
        uint8_t neighbor = world_get_block(w, nx, ny, nz);
        if ((self == BLOCK_LAVA && neighbor == BLOCK_WATER) ||
            (self == BLOCK_WATER && neighbor == BLOCK_LAVA)) {
            int lx = (self == BLOCK_LAVA) ? x : nx;
            int ly = (self == BLOCK_LAVA) ? y : ny;
            int lz = (self == BLOCK_LAVA) ? z : nz;
            world_set_block(w, lx, ly, lz, BLOCK_OBSIDIAN, true);
            audio_play_fizz(&a->audio);
            renderer_particles_place(&a->renderer, w, BLOCK_OBSIDIAN, lx, ly, lz, 0, 1, 0);
            return;
        }
    }
}

static void update_dropped_items(App *a, float dt) {
    Vec3 ppos = a->player.pos;
    ppos.y += 0.8f;

    for (int i = 0; i < a->dropped_count; i++) {
        if (!a->dropped_items[i].active)
            continue;
        a->dropped_items[i].age += dt;
        a->dropped_items[i].rot += 90.0f * dt;
        if (a->dropped_items[i].age > 300.0f) {
            a->dropped_items[i].active = false;
            continue;
        }

        a->dropped_items[i].vel.y -= 16.0f * dt;
        if (a->dropped_items[i].vel.y < -20.0f)
            a->dropped_items[i].vel.y = -20.0f;
        float drag = expf(-1.5f * dt);
        a->dropped_items[i].vel.x *= drag;
        a->dropped_items[i].vel.z *= drag;

        Vec3 np = vec3_add(a->dropped_items[i].pos, vec3_mul(a->dropped_items[i].vel, dt));
        int ibx = (int)floorf(np.x);
        int iby = (int)floorf(np.y);
        int ibz = (int)floorf(np.z);
        uint8_t cur_blk = world_get_block(&a->world, ibx, iby, ibz);

        /* Water buoyancy */
        if (cur_blk == BLOCK_WATER) {
            a->dropped_items[i].vel.y += 24.0f * dt;
            if (a->dropped_items[i].vel.y > 2.0f)
                a->dropped_items[i].vel.y = 2.0f;
        }

        if (block_is_solid(cur_blk)) {
            a->dropped_items[i].vel.y = 0;
            a->dropped_items[i].pos.y = (float)iby + 1.0f;
            a->dropped_items[i].vel.x = 0;
            a->dropped_items[i].vel.z = 0;
        } else {
            a->dropped_items[i].pos = np;
        }

        float dx = ppos.x - a->dropped_items[i].pos.x;
        float dy = ppos.y - a->dropped_items[i].pos.y;
        float dz = ppos.z - a->dropped_items[i].pos.z;
        float dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq < 3.5f && a->dropped_items[i].age > 0.4f) {
            float inv_d = 1.0f / sqrtf(dist_sq + 0.001f);
            a->dropped_items[i].pos.x += dx * inv_d * 7.0f * dt;
            a->dropped_items[i].pos.y += dy * inv_d * 7.0f * dt;
            a->dropped_items[i].pos.z += dz * inv_d * 7.0f * dt;

            if (dist_sq < 0.65f) {
                uint8_t blk = a->dropped_items[i].block;
                bool added = false;

                /* 1. Hotbar existing matching stack < 64 */
                for (int s = 0; s < HOTBAR_SLOTS; s++) {
                    if (a->player.hotbar[s] == blk && a->player.hotbar_counts[s] < 64) {
                        a->player.hotbar_counts[s]++;
                        added = true; break;
                    }
                }
                /* 2. Inventory existing matching stack < 64 */
                if (!added) {
                    for (int s = 0; s < INVENTORY_SLOTS; s++) {
                        if (a->player.inventory[s] == blk && a->player.inventory_counts[s] < 64) {
                            a->player.inventory_counts[s]++;
                            added = true; break;
                        }
                    }
                }
                /* 3. Empty hotbar slot */
                if (!added) {
                    for (int s = 0; s < HOTBAR_SLOTS; s++) {
                        if (a->player.hotbar[s] == BLOCK_AIR) {
                            a->player.hotbar[s] = blk;
                            a->player.hotbar_counts[s] = 1;
                            added = true; break;
                        }
                    }
                }
                /* 4. Empty inventory slot */
                if (!added) {
                    for (int s = 0; s < INVENTORY_SLOTS; s++) {
                        if (a->player.inventory[s] == BLOCK_AIR) {
                            a->player.inventory[s] = blk;
                            a->player.inventory_counts[s] = 1;
                            added = true; break;
                        }
                    }
                }

                if (added) {
                    a->dropped_items[i].active = false;
                    audio_play_pop(&a->audio);
                }
            }
        }
    }
}

static void spawn_active_tnt(App *a, float x, float y, float z);

static void explode_tnt(App *a, float ex, float ey, float ez) {
    audio_play_explode(&a->audio);
    float radius = 3.8f;
    int r = (int)ceilf(radius);
    int cx = (int)floorf(ex);
    int cy = (int)floorf(ey);
    int cz = (int)floorf(ez);

    for (int dy = -r; dy <= r; dy++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                float dist_sq = (float)(dx * dx + dy * dy + dz * dz);
                if (dist_sq > radius * radius)
                    continue;
                int bx = cx + dx;
                int by = cy + dy;
                int bz = cz + dz;
                uint8_t blk = world_get_block(&a->world, bx, by, bz);
                if (blk != BLOCK_AIR && blk != BLOCK_BEDROCK) {
                    world_set_block(&a->world, bx, by, bz, BLOCK_AIR, true);
                    renderer_particles_break(&a->renderer, blk, bx, by, bz);
                    if (blk == BLOCK_TNT) {
                        spawn_active_tnt(a, (float)bx + 0.5f, (float)by, (float)bz + 0.5f);
                    } else if ((rand() % 100) < 35) {
                        spawn_dropped_item(a, blk, bx, by, bz);
                    }
                }
            }
        }
    }

    for (int dz = -r; dz <= r; dz++) {
        for (int dx = -r; dx <= r; dx++) {
            trigger_falling_blocks(&a->world, a, cx + dx, cy + r + 1, cz + dz);
        }
    }

    float px = a->player.pos.x - ex;
    float py = (a->player.pos.y + 0.9f) - ey;
    float pz = a->player.pos.z - ez;
    float pdist = sqrtf(px * px + py * py + pz * pz);
    if (pdist < 6.5f) {
        float factor = 1.0f - (pdist / 6.5f);
        if (!a->player.creative) {
            player_damage(&a->player, factor * 16.0f);
        }
        if (pdist > 0.05f) {
            float k = factor * 1.6f;
            a->player.vel.x += (px / pdist) * k * 14.0f;
            a->player.vel.y += (py / pdist + 0.5f) * k * 10.0f;
            a->player.vel.z += (pz / pdist) * k * 14.0f;
        }
    }
}

static void spawn_active_tnt(App *a, float x, float y, float z) {
    int slot = -1;
    for (int i = 0; i < MAX_ACTIVE_TNT; i++) {
        if (!a->active_tnt[i].active) {
            slot = i; break;
        }
    }
    if (slot < 0) slot = 0;
    a->active_tnt[slot].active = true;
    a->active_tnt[slot].pos = vec3(x, y, z);
    float rx = ((float)(rand() % 100) / 50.0f - 1.0f) * 0.8f;
    float rz = ((float)(rand() % 100) / 50.0f - 1.0f) * 0.8f;
    a->active_tnt[slot].vel = vec3(rx, 3.0f, rz);
    a->active_tnt[slot].fuse = 3.5f;
    if (slot >= a->active_tnt_count)
        a->active_tnt_count = slot + 1;
    audio_play_fuse(&a->audio);
}

static void update_active_tnt(App *a, float dt) {
    for (int i = 0; i < a->active_tnt_count; i++) {
        if (!a->active_tnt[i].active)
            continue;
        a->active_tnt[i].fuse -= dt;
        if (a->active_tnt[i].fuse <= 0.0f) {
            a->active_tnt[i].active = false;
            explode_tnt(a, a->active_tnt[i].pos.x, a->active_tnt[i].pos.y, a->active_tnt[i].pos.z);
            continue;
        }

        a->active_tnt[i].vel.y -= 18.0f * dt;
        if (a->active_tnt[i].vel.y < -22.0f)
            a->active_tnt[i].vel.y = -22.0f;
        float drag = expf(-1.0f * dt);
        a->active_tnt[i].vel.x *= drag;
        a->active_tnt[i].vel.z *= drag;

        Vec3 np = vec3_add(a->active_tnt[i].pos, vec3_mul(a->active_tnt[i].vel, dt));
        int ibx = (int)floorf(np.x);
        int iby = (int)floorf(np.y);
        int ibz = (int)floorf(np.z);
        if (block_is_solid(world_get_block(&a->world, ibx, iby, ibz))) {
            a->active_tnt[i].vel.y = 0;
            a->active_tnt[i].pos.y = (float)iby + 1.0f;
            a->active_tnt[i].vel.x = 0;
            a->active_tnt[i].vel.z = 0;
        } else {
            a->active_tnt[i].pos = np;
        }
    }
}

static void update_playing(App *a, float dt) {
    PlayerInput in;
    memset(&in, 0, sizeof(in));
    if (a->ui.screen == UI_SCREEN_NONE) {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        in.forward = keys[SDL_SCANCODE_W] != 0;
        in.back = keys[SDL_SCANCODE_S] != 0;
        in.left = keys[SDL_SCANCODE_A] != 0;
        in.right = keys[SDL_SCANCODE_D] != 0;
        in.jump = keys[SDL_SCANCODE_SPACE] != 0;
        in.sneak = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;
        in.sprint = keys[SDL_SCANCODE_LCTRL] != 0 || keys[SDL_SCANCODE_RCTRL] != 0;
    }

    Vec3 before = a->player.pos;
    player_update(&a->player, &a->world, &in, dt);
    player_raycast(&a->player, &a->world, 6.0f);

    a->world_time += dt;
    ui_chat_update(&a->ui, dt);

    /* Hurt sound & water splash */
    if (a->player.hurt_timer > 0.35f && a->player.hurt_timer <= 0.40f) {
        audio_play_hurt(&a->audio);
    }
    if (a->player.in_water && !a->player.was_in_water) {
        audio_play_splash(&a->audio);
    }
    a->player.was_in_water = a->player.in_water;

    update_dropped_items(a, dt);
    update_active_tnt(a, dt);
    entity_update_all(&a->entities, &a->world, &a->player, &a->audio, dt, a->world_time);

    player_update_furnace(&a->player, dt);

    float dx = a->player.pos.x - before.x;
    float dz = a->player.pos.z - before.z;
    float moved = sqrtf(dx * dx + dz * dz);
    if (a->player.on_ground && !a->player.in_water) {
        a->step_accum += moved;
        if (a->step_accum > 2.0f) {
            a->step_accum = 0;
            int bx = (int)floorf(a->player.pos.x);
            int by = (int)floorf(a->player.pos.y - 0.15f);
            int bz = (int)floorf(a->player.pos.z);
            uint8_t under = world_get_block(&a->world, bx, by, bz);
            if (under != BLOCK_AIR)
                audio_play_step(&a->audio, under);
        }
    } else {
        a->step_accum = 0;
    }

    a->player.break_cooldown -= dt;
    a->player.place_cooldown -= dt;
    if (a->ui.screen == UI_SCREEN_NONE) {
        if (a->mouse_left) {
            a->player.swing_timer = 0.25f;

            /* Check entity hit (mob melee attack) */
            Vec3 eye = player_eye(&a->player);
            Vec3 look = player_forward(&a->player);
            Entity *hit_ent = entity_raycast(&a->entities, eye, look, 3.8f);
            if (hit_ent && a->player.break_cooldown <= 0.0f) {
                uint8_t held = a->player.hotbar[a->player.selected];
                float dmg = 1.0f;
                if (held == ITEM_WOODEN_SWORD) dmg = 4.0f;
                else if (held == ITEM_STONE_SWORD) dmg = 5.0f;
                else if (held == ITEM_IRON_SWORD) dmg = 6.0f;
                else if (held == ITEM_DIAMOND_SWORD) dmg = 7.0f;
                else if (held == ITEM_MACE) dmg = 9.0f;
                else if (held == ITEM_DIAMOND_AXE) dmg = 9.0f;
                else if (held == ITEM_IRON_AXE) dmg = 8.0f;

                Vec3 kb = vec3_mul(look, 7.0f);
                kb.y = 4.0f;
                bool killed = entity_damage(&a->entities, hit_ent, dmg, kb, &a->audio);
                if (killed) {
                    if (hit_ent->type == ENTITY_ZOMBIE)
                        spawn_dropped_item(a, ITEM_ROTTEN_FLESH, (int)hit_ent->pos.x, (int)hit_ent->pos.y, (int)hit_ent->pos.z);
                    else if (hit_ent->type == ENTITY_SKELETON) {
                        spawn_dropped_item(a, ITEM_BONE, (int)hit_ent->pos.x, (int)hit_ent->pos.y, (int)hit_ent->pos.z);
                        spawn_dropped_item(a, ITEM_ARROW, (int)hit_ent->pos.x, (int)hit_ent->pos.y, (int)hit_ent->pos.z);
                    } else if (hit_ent->type == ENTITY_PIG)
                        spawn_dropped_item(a, ITEM_COOKED_BEEF, (int)hit_ent->pos.x, (int)hit_ent->pos.y, (int)hit_ent->pos.z);
                    else if (hit_ent->type == ENTITY_CREEPER)
                        spawn_dropped_item(a, ITEM_REDSTONE, (int)hit_ent->pos.x, (int)hit_ent->pos.y, (int)hit_ent->pos.z);
                }
                a->player.break_cooldown = 0.35f;
            } else if (a->player.target_valid) {
                int bx = a->player.target_x;
                int by = a->player.target_y;
                int bz = a->player.target_z;
                uint8_t b = a->player.target_block;

                if (b == BLOCK_TNT) {
                    world_set_block(&a->world, bx, by, bz, BLOCK_AIR, true);
                    spawn_active_tnt(a, (float)bx + 0.5f, (float)by, (float)bz + 0.5f);
                    a->player.swing_timer = 0.25f;
                    a->player.break_cooldown = 0.25f;
                    player_raycast(&a->player, &a->world, 6.0f);
                } else if (a->player.creative || a->player.flying) {
                    if (a->player.break_cooldown <= 0) {
                        int broke = player_break(&a->player, &a->world);
                        if (broke) {
                            renderer_particles_break(&a->renderer, (uint8_t)broke, bx, by, bz);
                            audio_play_break(&a->audio, (uint8_t)broke);
                            spawn_dropped_item(a, (uint8_t)broke, bx, by, bz);
                            trigger_falling_blocks(&a->world, a, bx, by + 1, bz);
                            check_water_lava_at(&a->world, a, bx, by, bz);
                            a->player.break_cooldown = 0.22f;
                            player_raycast(&a->player, &a->world, 6.0f);
                        }
                    }
                } else {
                    if (bx != a->player.mining_x || by != a->player.mining_y || bz != a->player.mining_z) {
                        a->player.mining_x = bx;
                        a->player.mining_y = by;
                        a->player.mining_z = bz;
                        a->player.mining_progress = 0.0f;
                        a->player.mining_sound_timer = 0.0f;
                    }
                    uint8_t held = a->player.hotbar[a->player.selected];
                    float break_time = block_break_time(b, held);
                    if (break_time > 0.0f) {
                        a->player.mining_progress += dt / break_time;
                        a->player.mining_sound_timer -= dt;
                        if (a->player.mining_sound_timer <= 0.0f) {
                            a->player.mining_sound_timer = 0.22f;
                            audio_play_dig(&a->audio, b);
                            renderer_particles_place(&a->renderer, &a->world, b, bx, by, bz,
                                                     a->player.target_nx, a->player.target_ny, a->player.target_nz);
                        }
                        if (a->player.mining_progress >= 1.0f) {
                            int broke = player_break(&a->player, &a->world);
                            if (broke) {
                                renderer_particles_break(&a->renderer, (uint8_t)broke, bx, by, bz);
                                audio_play_break(&a->audio, (uint8_t)broke);
                                uint8_t drop = block_drop_item((uint8_t)broke, held);
                                if (drop != BLOCK_AIR) {
                                    spawn_dropped_item(a, drop, bx, by, bz);
                                }
                                trigger_falling_blocks(&a->world, a, bx, by + 1, bz);
                                check_water_lava_at(&a->world, a, bx, by, bz);
                                a->player.mining_progress = 0.0f;
                                a->player.break_cooldown = 0.15f;
                                player_raycast(&a->player, &a->world, 6.0f);
                            }
                        }
                    }
                }
            } else {
                a->player.mining_progress = 0.0f;
            }
        } else {
            a->player.mining_progress = 0.0f;
        }

        if (a->mouse_right && a->player.place_cooldown <= 0) {
            uint8_t held = a->player.hotbar[a->player.selected];
            if (item_is_armor(held)) {
                int aslot = armor_slot_for_item(held);
                if (aslot >= 0) {
                    uint8_t old_armor = a->player.armor[aslot];
                    a->player.armor[aslot] = held;
                    a->player.hotbar_counts[a->player.selected]--;
                    if (a->player.hotbar_counts[a->player.selected] == 0)
                        a->player.hotbar[a->player.selected] = old_armor;
                    else if (old_armor != BLOCK_AIR)
                        spawn_dropped_item_vec(a, old_armor, a->player.pos);
                    player_update_armor_points(&a->player);
                    a->player.place_cooldown = 0.3f;
                }
            } else if (held == ITEM_BOW) {
                bool has_arrow = a->player.creative;
                if (!has_arrow) {
                    for (int s = 0; s < HOTBAR_SLOTS; s++) {
                        if (a->player.hotbar[s] == ITEM_ARROW && a->player.hotbar_counts[s] > 0) {
                            has_arrow = true;
                            a->player.hotbar_counts[s]--;
                            if (a->player.hotbar_counts[s] == 0) a->player.hotbar[s] = BLOCK_AIR;
                            break;
                        }
                    }
                    if (!has_arrow) {
                        for (int s = 0; s < INVENTORY_SLOTS; s++) {
                            if (a->player.inventory[s] == ITEM_ARROW && a->player.inventory_counts[s] > 0) {
                                has_arrow = true;
                                a->player.inventory_counts[s]--;
                                if (a->player.inventory_counts[s] == 0) a->player.inventory[s] = BLOCK_AIR;
                                break;
                            }
                        }
                    }
                }
                if (has_arrow) {
                    Vec3 eye = player_eye(&a->player);
                    Vec3 dir = player_forward(&a->player);
                    Entity *arr = entity_spawn(&a->entities, ENTITY_ARROW, eye);
                    if (arr) {
                        arr->vel = vec3_mul(dir, 26.0f);
                        audio_play_bow(&a->audio);
                    }
                    a->player.place_cooldown = 0.35f;
                }
            } else if (a->player.target_valid && a->player.target_block == BLOCK_CRAFTING_TABLE) {
                ui_set_screen(&a->ui, UI_SCREEN_CRAFTING, true);
                SDL_SetRelativeMouseMode(SDL_FALSE);
                a->mouse_right = false;
                a->player.place_cooldown = 0.3f;
            } else if (a->player.target_valid && a->player.target_block == BLOCK_FURNACE) {
                ui_set_screen(&a->ui, UI_SCREEN_FURNACE, true);
                SDL_SetRelativeMouseMode(SDL_FALSE);
                a->mouse_right = false;
                a->player.place_cooldown = 0.3f;
            } else if (a->player.target_valid && a->player.target_block == BLOCK_CHEST) {
                ui_set_screen(&a->ui, UI_SCREEN_CHEST, true);
                audio_play_chest_open(&a->audio);
                SDL_SetRelativeMouseMode(SDL_FALSE);
                a->mouse_right = false;
                a->player.place_cooldown = 0.3f;
            } else if (a->player.target_valid && a->player.target_block == BLOCK_TNT) {
                int bx = a->player.target_x;
                int by = a->player.target_y;
                int bz = a->player.target_z;
                world_set_block(&a->world, bx, by, bz, BLOCK_AIR, true);
                spawn_active_tnt(a, (float)bx + 0.5f, (float)by, (float)bz + 0.5f);
                a->player.swing_timer = 0.20f;
                a->player.place_cooldown = 0.25f;
                player_raycast(&a->player, &a->world, 6.0f);
            } else if (a->player.target_valid && (a->player.target_block == BLOCK_OAK_DOOR || a->player.target_block == BLOCK_OAK_DOOR_OPEN)) {
                int bx = a->player.target_x, by = a->player.target_y, bz = a->player.target_z;
                bool opening = (a->player.target_block == BLOCK_OAK_DOOR);
                world_set_block(&a->world, bx, by, bz, opening ? BLOCK_OAK_DOOR_OPEN : BLOCK_OAK_DOOR, true);
                if (opening) audio_play_door_open(&a->audio);
                else audio_play_door_close(&a->audio);
                a->player.swing_timer = 0.20f;
                a->player.place_cooldown = 0.25f;
                player_raycast(&a->player, &a->world, 6.0f);
            } else if (a->player.target_valid && (a->player.target_block == BLOCK_OAK_TRAPDOOR || a->player.target_block == BLOCK_OAK_TRAPDOOR_OPEN)) {
                int bx = a->player.target_x, by = a->player.target_y, bz = a->player.target_z;
                bool opening = (a->player.target_block == BLOCK_OAK_TRAPDOOR);
                world_set_block(&a->world, bx, by, bz, opening ? BLOCK_OAK_TRAPDOOR_OPEN : BLOCK_OAK_TRAPDOOR, true);
                if (opening) audio_play_door_open(&a->audio);
                else audio_play_door_close(&a->audio);
                a->player.swing_timer = 0.20f;
                a->player.place_cooldown = 0.25f;
                player_raycast(&a->player, &a->world, 6.0f);
            } else if (a->player.target_valid && (a->player.target_block == BLOCK_LEVER || a->player.target_block == BLOCK_STONE_BUTTON)) {
                int bx = a->player.target_x, by = a->player.target_y, bz = a->player.target_z;
                uint8_t cur_p = world_get_power(&a->world, bx, by, bz);
                world_set_power(&a->world, bx, by, bz, cur_p > 0 ? 0 : 15);
                audio_play_click(&a->audio);
                world_update_redstone_at(&a->world, bx, by, bz);
                a->player.swing_timer = 0.20f;
                a->player.place_cooldown = 0.25f;
                player_raycast(&a->player, &a->world, 6.0f);
            } else {
                float f_food = 0, f_sat = 0;
                if (item_is_food(held, &f_food, &f_sat)) {
                    if (a->player.hunger < 20.0f || a->player.creative) {
                        a->player.hunger = fminf(20.0f, a->player.hunger + f_food);
                        a->player.saturation = fminf(a->player.hunger, a->player.saturation + f_sat);
                        audio_play_eat(&a->audio);
                        a->player.swing_timer = 0.25f;
                        a->player.place_cooldown = 0.35f;
                        if (!a->player.creative) {
                            if (a->player.hotbar_counts[a->player.selected] > 1) {
                                a->player.hotbar_counts[a->player.selected]--;
                            } else {
                                a->player.hotbar[a->player.selected] = BLOCK_AIR;
                                a->player.hotbar_counts[a->player.selected] = 0;
                            }
                        }
                    }
                } else if (held == ITEM_BUCKET) {
                    if (player_raycast_ex(&a->player, &a->world, 6.0f, true)) {
                        uint8_t tb = a->player.target_block;
                        if (tb == BLOCK_WATER || tb == BLOCK_LAVA) {
                            world_set_block(&a->world, a->player.target_x, a->player.target_y, a->player.target_z, BLOCK_AIR, true);
                            uint8_t filled = (tb == BLOCK_WATER) ? ITEM_WATER_BUCKET : ITEM_LAVA_BUCKET;
                            if (tb == BLOCK_WATER) audio_play_splash(&a->audio);
                            else audio_play_fizz(&a->audio);
                            a->player.swing_timer = 0.20f;
                            a->player.place_cooldown = 0.25f;
                            if (!a->player.creative) {
                                if (a->player.hotbar_counts[a->player.selected] > 1) {
                                    a->player.hotbar_counts[a->player.selected]--;
                                    refund_item_or_drop(a, filled, 1);
                                } else {
                                    a->player.hotbar[a->player.selected] = filled;
                                    a->player.hotbar_counts[a->player.selected] = 1;
                                }
                            }
                            player_raycast(&a->player, &a->world, 6.0f);
                        }
                    }
                } else if (held == ITEM_WATER_BUCKET || held == ITEM_LAVA_BUCKET) {
                    if (player_raycast(&a->player, &a->world, 6.0f)) {
                        int px = a->player.target_x + a->player.target_nx;
                        int py = a->player.target_y + a->player.target_ny;
                        int pz = a->player.target_z + a->player.target_nz;
                        uint8_t liquid = (held == ITEM_WATER_BUCKET) ? BLOCK_WATER : BLOCK_LAVA;
                        if (py >= 0 && py < WORLD_HEIGHT &&
                            (world_get_block(&a->world, px, py, pz) == BLOCK_AIR ||
                             g_block_defs[world_get_block(&a->world, px, py, pz)].replaceable)) {
                            world_set_block(&a->world, px, py, pz, liquid, true);
                            if (liquid == BLOCK_WATER) audio_play_splash(&a->audio);
                            else audio_play_fizz(&a->audio);
                            a->player.swing_timer = 0.20f;
                            a->player.place_cooldown = 0.25f;
                            if (!a->player.creative) {
                                a->player.hotbar[a->player.selected] = ITEM_BUCKET;
                                a->player.hotbar_counts[a->player.selected] = 1;
                            }
                            player_raycast(&a->player, &a->world, 6.0f);
                        }
                    }
                } else if (held != BLOCK_AIR && (!g_block_defs[held].is_item || held == ITEM_REDSTONE || held == ITEM_SUGAR_CANE)) {
                    uint8_t to_place = (held == ITEM_REDSTONE) ? BLOCK_REDSTONE_WIRE :
                                       (held == ITEM_SUGAR_CANE) ? BLOCK_SUGAR_CANE : held;
                    int px = a->player.target_x + a->player.target_nx;
                    int py = a->player.target_y + a->player.target_ny;
                    int pz = a->player.target_z + a->player.target_nz;
                    int nx = a->player.target_nx;
                    int ny = a->player.target_ny;
                    int nz = a->player.target_nz;
                    if (player_place(&a->player, &a->world, to_place)) {
                        a->player.swing_timer = 0.20f;
                        renderer_particles_place(&a->renderer, &a->world, held, px, py, pz,
                                                 nx, ny, nz);
                        audio_play_place(&a->audio, held);
                        a->player.place_cooldown = 0.20f;
                        if (!a->player.creative) {
                            if (a->player.hotbar_counts[a->player.selected] > 1) {
                                a->player.hotbar_counts[a->player.selected]--;
                            } else {
                                a->player.hotbar[a->player.selected] = BLOCK_AIR;
                                a->player.hotbar_counts[a->player.selected] = 0;
                            }
                        }
                        trigger_falling_blocks(&a->world, a, px, py, pz);
                        check_water_lava_at(&a->world, a, px, py, pz);
                        player_raycast(&a->player, &a->world, 6.0f);
                    }
                }
            }
        }
    } else {
        a->player.mining_progress = 0.0f;
    }

    world_update(&a->world, a->player.pos.x, a->player.pos.z);
    renderer_particles_update(&a->renderer, &a->world, dt);

    a->save_timer += dt;
    if (a->save_timer > 30.0f) {
        a->save_timer = 0;
        save_world(a, false);
    }

    a->cave_timer -= dt;
    if (a->cave_timer <= 0) {
        a->cave_timer = 55.0f + (float)(rand() % 60);
        if (a->player.pos.y < 46.0f)
            audio_play_cave(&a->audio);
    }

    if (a->toast_timer > 0)
        a->toast_timer -= dt;

    if (a->player.dead) {
        a->mouse_left = false;
        a->mouse_right = false;
        ui_set_screen(&a->ui, UI_SCREEN_DEATH, a->has_save);
        update_mouse_capture(a);
    }
}

static void render_frame(App *a) {
    if (a->world_active && a->ui.screen != UI_SCREEN_TITLE &&
        a->ui.screen != UI_SCREEN_LOADING) {
        Vec3 eye = player_eye(&a->player);
        if (a->settings.view_bobbing && a->player.on_ground) {
            float speed = sqrtf(a->player.vel.x * a->player.vel.x +
                                a->player.vel.z * a->player.vel.z);
            if (speed > 0.5f)
                eye.y += sinf(a->player.bob * 2.0f) * 0.035f;
        }
        renderer_sky(&a->renderer, a->world_time, eye, a->player.yaw, a->player.pitch);
        renderer_world(&a->renderer, &a->world, eye,
                       a->player.yaw, a->player.pitch, 32);
        renderer_clouds(&a->renderer, a->world_time, eye);
        renderer_particles_draw(&a->renderer, eye,
                                a->player.yaw, a->player.pitch);

        for (int i = 0; i < a->dropped_count; i++) {
            if (a->dropped_items[i].active)
                renderer_dropped_item(&a->renderer, a->dropped_items[i].block,
                                      a->dropped_items[i].pos,
                                      a->dropped_items[i].rot,
                                      a->dropped_items[i].age);
        }

        for (int i = 0; i < a->active_tnt_count; i++) {
            if (a->active_tnt[i].active)
                renderer_tnt(&a->renderer, a->active_tnt[i].pos, a->active_tnt[i].fuse);
        }

        entity_render_all(&a->entities, &a->renderer, &a->player.pos);

        if (a->ui.screen == UI_SCREEN_NONE || a->ui.screen == UI_SCREEN_PAUSE) {
            if (a->player.target_valid) {
                renderer_outline(&a->renderer, a->player.target_x,
                                 a->player.target_y, a->player.target_z, true);
                if (a->player.mining_progress > 0.0f) {
                    renderer_draw_break_progress(&a->renderer,
                                                 a->player.mining_x,
                                                 a->player.mining_y,
                                                 a->player.mining_z,
                                                 a->player.mining_progress);
                }
            }
        }

        if (a->ui.screen == UI_SCREEN_NONE) {
            uint8_t held = a->player.hotbar[a->player.selected];
            renderer_hand(&a->renderer, held, a->player.swing_timer,
                          a->settings.view_bobbing ? a->player.bob : 0.0f, a->player.in_water);
        }
    } else {
        renderer_clear(&a->renderer);
    }

    a->hud.fps = a->fps;
    a->hud.loaded_chunks = a->world.chunk_count;
    a->hud.mesh_chunks = a->renderer.chunk_count;
    a->hud.quads = a->renderer.last_quads;
    a->hud.seed = a->world.seed;
    a->hud.render_x = a->player.pos.x;
    a->hud.render_z = a->player.pos.z;
    a->hud.show_debug = a->ui.show_debug;
    a->hud.toast = a->toast;
    a->hud.toast_time = a->toast_timer;

    ui_draw(&a->renderer, &a->ui, &a->player, &a->world, &a->hud);
    present_screen(a);
}

static void init_sdl(App *a) {
    if (a->headless) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            die("SDL initialization failed");
#if MCED_USE_GL
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        a->window = SDL_CreateWindow("Minecraft C Edition (Headless)",
                                     0, 0, a->win_w, a->win_h,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (a->window)
            a->gl_context = SDL_GL_CreateContext(a->window);
        int dw = a->win_w, dh = a->win_h;
        if (a->window)
            SDL_GL_GetDrawableSize(a->window, &dw, &dh);
        a->render_w = dw;
        a->render_h = dh;
#else
        a->render_w = a->internal_w;
        a->render_h = a->internal_h;
#endif
        return;
    }
    /* Keep look sensitivity independent of the renderer's logical size. */
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SCALING, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
        die("SDL initialization failed");
    /* SDL's X11 relative mode fights with XWayland's pointer warps: the
       warps come back as relative motion and the camera drifts by itself.
       Use absolute cursor tracking there; native X11/Wayland still get
       normal relative mouse mode. */
    const char *driver = SDL_GetCurrentVideoDriver();
    const char *wayland = getenv("WAYLAND_DISPLAY");
    a->soft_look = !a->force_relative_mouse && driver &&
                   strcmp(driver, "x11") == 0 && wayland && wayland[0];
    printf("mouse: %s\n", a->soft_look ? "absolute (XWayland)"
                                       : "relative");

#if MCED_USE_GL
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    a->window = SDL_CreateWindow("Minecraft C Edition",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 a->win_w, a->win_h,
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!a->window)
        die("could not create window");

    a->gl_context = SDL_GL_CreateContext(a->window);
    if (!a->gl_context)
        die("could not create OpenGL context");

    /* Disable VSync by default for maximum performance (300+ FPS) */
    SDL_GL_SetSwapInterval(a->vsync ? 1 : 0);

    int dw = a->win_w, dh = a->win_h;
    SDL_GL_GetDrawableSize(a->window, &dw, &dh);
    a->render_w = dw;
    a->render_h = dh;
#else
    if (a->win_w < 960) {
        a->win_w = 1280;
        a->win_h = 720;
    }
    a->window = SDL_CreateWindow("Minecraft C Edition",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 a->win_w, a->win_h,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!a->window)
        die("could not create window");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    a->sdl = SDL_CreateRenderer(a->window, -1,
                                SDL_RENDERER_ACCELERATED | (a->vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!a->sdl)
        a->sdl = SDL_CreateRenderer(a->window, -1, SDL_RENDERER_SOFTWARE);
    if (!a->sdl)
        die("could not create SDL renderer");
    SDL_RenderSetLogicalSize(a->sdl, a->internal_w, a->internal_h);
    a->screen = SDL_CreateTexture(a->sdl, SDL_PIXELFORMAT_RGB888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  a->internal_w, a->internal_h);
    if (!a->screen)
        die("could not create screen texture");
    a->render_w = a->internal_w;
    a->render_h = a->internal_h;
#endif
}

static void shutdown_app(App *a) {
    if (a->pool) {
        thread_pool_wait_idle(a->pool);
    }
    if (a->world_active) {
        save_world(a, true);
        world_destroy(&a->world);
        a->world_active = false;
    }
    renderer_reset_chunks(&a->renderer);
    renderer_shutdown(&a->renderer);
    if (a->pool) {
        thread_pool_destroy(a->pool);
        a->pool = NULL;
    }
    audio_shutdown(&a->audio);
    assets_free(&a->assets);
#if MCED_USE_GL
    if (a->gl_context) SDL_GL_DeleteContext(a->gl_context);
#endif
    if (a->screen) SDL_DestroyTexture(a->screen);
    if (a->sdl) SDL_DestroyRenderer(a->sdl);
    if (a->window) SDL_DestroyWindow(a->window);
    SDL_Quit();
}

static void usage(const char *argv0) {
#if MCED_USE_GL
    printf("Minecraft C Edition - Hardware Accelerated OpenGL voxel sandbox\n\n");
#else
    printf("Minecraft C Edition - a TinyGL voxel sandbox\n\n");
#endif
    printf("Usage: %s [options]\n", argv0);
    printf("  --width N --height N     window / render resolution (default 1920x1080)\n");
    printf("  --vsync                  enable vertical synchronization (default off for 300+ FPS)\n");
    printf("  --max-fps N              cap maximum frame rate (default 0: uncapped)\n");
    printf("  --screenshot FILE        render a world screenshot and exit\n");
    printf("  --frames N               frames to render before screenshot (default 3)\n");
    printf("  --menu                   screenshot the title screen instead\n");
    printf("  --screen NAME            pause, inventory, or debug overlay\n");
    printf("  --seed N                 world seed\n");
    printf("  --pos X Y Z              place the camera in the world\n");
    printf("  --yaw DEG --pitch DEG    initial camera angles\n");
    printf("  --no-audio               disable sound\n");
    printf("  --relative-mouse         force SDL relative mouse mode\n");
    printf("  --play                   skip the title, load the last world\n");
    printf("  --render-distance N      chunks of view distance (2-32, default 16)\n");
    printf("  --fullscreen             start fullscreen\n");
    printf("  --help                   show this text\n\n");
    printf("Controls: WASD move, Space jump/swim, Shift sneak/descend, Ctrl sprint,\n");
    printf("          F fly, E inventory, F3 debug, F2 screenshot, F11 fullscreen,\n");
    printf("          Esc pause, M mute, 1-9 hotbar, mouse look, L/R click break/place.\n\n");
    printf("Assets are imported from the local Minecraft installation and are for\n");
    printf("personal use only. Do not redistribute Mojang assets.\n");
}

int main(int argc, char **argv) {
    App app;
    memset(&app, 0, sizeof(app));
#if MCED_USE_GL
    app.win_w = 1920;
    app.win_h = 1080;
    app.internal_w = 1920;
    app.internal_h = 1080;
#else
    app.win_w = 1280;
    app.win_h = 720;
    app.internal_w = 480;
    app.internal_h = 270;
#endif
    app.screenshot_frames = 3;
    app.running = true;
    app.fps = 60.0f;
    app.vsync = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--width") && i + 1 < argc) {
            app.internal_w = atoi(argv[++i]);
            app.win_w = app.internal_w;
        } else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
            app.internal_h = atoi(argv[++i]);
            app.win_h = app.internal_h;
        } else if (!strcmp(argv[i], "--vsync")) {
            app.vsync = true;
        } else if (!strcmp(argv[i], "--max-fps") && i + 1 < argc) {
            app.max_fps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            app.screenshot_path = argv[++i];
            app.headless = true;
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            app.screenshot_frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--menu")) {
            app.screenshot_menu = true;
        } else if (!strcmp(argv[i], "--screen") && i + 1 < argc) {
            app.screenshot_screen = argv[++i];
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            app.seed_override = strtoull(argv[++i], NULL, 10);
            app.has_seed_override = true;
        } else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) {
            app.forced_yaw = (float)atof(argv[++i]);
            app.has_yaw = true;
        } else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) {
            app.forced_pitch = (float)atof(argv[++i]);
            app.has_pitch = true;
        } else if (!strcmp(argv[i], "--pos") && i + 3 < argc) {
            app.eye_override[0] = (float)atof(argv[++i]);
            app.eye_override[1] = (float)atof(argv[++i]);
            app.eye_override[2] = (float)atof(argv[++i]);
            app.has_eye_override = true;
        } else if (!strcmp(argv[i], "--no-audio")) {
            app.no_audio = true;
        } else if (!strcmp(argv[i], "--relative-mouse")) {
            app.force_relative_mouse = true;
        } else if (!strcmp(argv[i], "--play")) {
            app.play_direct = true;
        } else if (!strcmp(argv[i], "--render-distance") && i + 1 < argc) {
            app.render_distance = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--fullscreen")) {
            app.fullscreen = true;
        } else if (!strcmp(argv[i], "--time") && i + 1 < argc) {
            app.time_override = (float)atof(argv[++i]);
            app.has_time_override = true;
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc) {
            app.scene_name = argv[++i];
        } else if (!strcmp(argv[i], "--inv-page") && i + 1 < argc) {
            app.inv_page_override = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--empty-hand")) {
            app.held_item_override = BLOCK_AIR;
            app.has_held_override = true;
        } else if (!strcmp(argv[i], "--held") && i + 1 < argc) {
            app.held_item_override = atoi(argv[++i]);
            app.has_held_override = true;
        } else {
            fprintf(stderr, "unknown option: %s (try --help)\n", argv[i]);
            return 1;
        }
    }

    if (app.win_w < 160 || app.win_h < 90 ||
        app.win_w > 8192 || app.win_h > 8192) {
        fprintf(stderr, "invalid resolution\n");
        return 1;
    }
    app.internal_w &= ~1;
    app.win_w &= ~1;

    init_sdl(&app);
    if (!assets_load(&app.assets, argv[0])) {
        fprintf(stderr, "run: python3 tools/extract_assets.py\n");
        return 1;
    }
    blocks_init(&app.assets);
#if MCED_USE_GL
    if (!renderer_init(&app.renderer, &app.assets, app.render_w, app.render_h))
        die("could not initialize OpenGL renderer");
    ui_init(&app.ui, app.renderer.ui_width, app.renderer.ui_height);
#else
    if (!renderer_init(&app.renderer, &app.assets, app.internal_w, app.internal_h))
        die("could not initialize TinyGL");
    ui_init(&app.ui, app.internal_w, app.internal_h);
#endif
    app.pool = thread_pool_create(0);
    if (app.pool) {
        renderer_set_thread_pool(&app.renderer, app.pool);
        world_set_thread_pool(&app.world, app.pool);
        printf("Parallel execution enabled: thread pool initialized with %d worker threads\n",
               thread_pool_thread_count(app.pool));
    }
    derive_paths(&app);
    app.has_save = access(app.save_path, R_OK) == 0;
    settings_load(&app.settings, app.options_path);
    if (app.render_distance)
        app.settings.render_distance = app.render_distance < 2 ? 2 :
                                     (app.render_distance > 32 ? 32 : app.render_distance);
    if (app.vsync)
        app.settings.vsync = true;
    if (app.fullscreen)
        app.settings.fullscreen = true;
    if (app.max_fps)
        app.settings.max_fps = app.max_fps;
    if (app.no_audio)
        app.settings.mute = true;

    ui_set_settings(&app.ui, &app.settings);
    apply_settings(&app);

    if (!app.no_audio && !app.headless) {
        if (!audio_init(&app.audio, &app.assets)) {
            fprintf(stderr, "continuing without sound\n");
            audio_shutdown(&app.audio);
        }
    }

    printf("saves: %s (%s)\n", app.save_dir, app.has_save ? "existing world" : "empty");
    printf("seed: %llu\n", (unsigned long long)(app.has_seed_override ? app.seed_override : 0));

    if (app.screenshot_path) {
        app.no_save = true;
        if (app.screenshot_menu) {
            if (app.screenshot_screen) {
                if (!strcmp(app.screenshot_screen, "create_world")) {
                    ui_set_screen(&app.ui, UI_SCREEN_CREATE_WORLD, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "loading")) {
                    ui_draw_loading(&app.renderer, "Building terrain", 0.68f);
                    bool ok = renderer_screenshot(&app.renderer, app.screenshot_path);
                    shutdown_app(&app);
                    return ok ? 0 : 1;
                } else if (!strcmp(app.screenshot_screen, "options")) {
                    ui_set_screen(&app.ui, UI_SCREEN_OPTIONS, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "video")) {
                    ui_set_screen(&app.ui, UI_SCREEN_VIDEO, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "audio")) {
                    ui_set_screen(&app.ui, UI_SCREEN_AUDIO, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "controls")) {
                    ui_set_screen(&app.ui, UI_SCREEN_CONTROLS, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "advancements")) {
                    ui_set_screen(&app.ui, UI_SCREEN_ADVANCEMENTS, app.has_save);
                } else {
                    ui_set_screen(&app.ui, UI_SCREEN_TITLE, app.has_save);
                }
            } else {
                ui_set_screen(&app.ui, UI_SCREEN_TITLE, app.has_save);
            }
            renderer_clear(&app.renderer);
            ui_draw(&app.renderer, &app.ui, &app.player, &app.world, &app.hud);
        } else {
            if (!app.has_seed_override)
                app.seed_override = 424242, app.has_seed_override = true;
            if (!app.scene_name) {
                if (!app.has_yaw)
                    app.forced_yaw = 35.0f, app.has_yaw = true;
                if (!app.has_pitch)
                    app.forced_pitch = -14.0f, app.has_pitch = true;
            }
            start_world(&app, true);
            if (app.has_held_override) {
                app.player.hotbar[app.player.selected] = (uint8_t)app.held_item_override;
                app.player.hotbar_counts[app.player.selected] = (app.held_item_override == BLOCK_AIR) ? 0 : 64;
            }
            if (app.has_eye_override) {
                app.player.pos = vec3(app.eye_override[0], app.eye_override[1],
                                      app.eye_override[2]);
                app.player.flying = true;
            }
            if (app.screenshot_screen) {
                if (!strcmp(app.screenshot_screen, "pause")) {
                    ui_set_screen(&app.ui, UI_SCREEN_PAUSE, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "inventory") ||
                           !strcmp(app.screenshot_screen, "inventory_survival")) {
                    app.player.creative = false;
                    app.player.inventory[0] = BLOCK_OAK_LOG;
                    app.player.inventory_counts[0] = 32;
                    app.player.inventory[1] = BLOCK_COBBLESTONE;
                    app.player.inventory_counts[1] = 64;
                    app.player.inventory[2] = BLOCK_IRON_BLOCK;
                    app.player.inventory_counts[2] = 12;
                    app.player.crafting[0] = BLOCK_OAK_PLANKS;
                    app.player.crafting_counts[0] = 4;
                    app.player.crafting[1] = BLOCK_OAK_PLANKS;
                    app.player.crafting_counts[1] = 4;
                    app.player.craft_result = BLOCK_TORCH;
                    app.player.craft_result_count = 4;
                    ui_set_screen(&app.ui, UI_SCREEN_INVENTORY, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "inventory_creative")) {
                    app.player.creative = true;
                    ui_set_screen(&app.ui, UI_SCREEN_INVENTORY, app.has_save);
                    if (app.inv_page_override > 0)
                        app.ui.inv_page = app.inv_page_override;
                } else if (!strcmp(app.screenshot_screen, "crafting")) {
                    app.player.creative = false;
                    app.player.crafting_3x3[0] = BLOCK_OAK_PLANKS; app.player.crafting_3x3_counts[0] = 3;
                    app.player.crafting_3x3[1] = BLOCK_OAK_PLANKS; app.player.crafting_3x3_counts[1] = 3;
                    app.player.crafting_3x3[2] = BLOCK_OAK_PLANKS; app.player.crafting_3x3_counts[2] = 3;
                    app.player.crafting_3x3[4] = ITEM_STICK;       app.player.crafting_3x3_counts[4] = 2;
                    app.player.crafting_3x3[7] = ITEM_STICK;       app.player.crafting_3x3_counts[7] = 2;
                    player_update_crafting_3x3(&app.player);
                    app.player.inventory[0] = BLOCK_COBBLESTONE; app.player.inventory_counts[0] = 64;
                    app.player.inventory[1] = ITEM_IRON_INGOT;   app.player.inventory_counts[1] = 16;
                    app.player.inventory[2] = ITEM_DIAMOND;      app.player.inventory_counts[2] = 8;
                    app.player.hotbar[0] = ITEM_DIAMOND_PICKAXE; app.player.hotbar_counts[0] = 1;
                    app.player.hotbar[1] = ITEM_DIAMOND_SWORD;   app.player.hotbar_counts[1] = 1;
                    app.player.hotbar[2] = ITEM_BREAD;           app.player.hotbar_counts[2] = 12;
                    ui_set_screen(&app.ui, UI_SCREEN_CRAFTING, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "furnace")) {
                    app.player.creative = false;
                    app.player.furnace_input = ITEM_RAW_IRON;
                    app.player.furnace_input_count = 14;
                    app.player.furnace_fuel = ITEM_COAL;
                    app.player.furnace_fuel_count = 5;
                    app.player.furnace_output = ITEM_IRON_INGOT;
                    app.player.furnace_output_count = 2;
                    app.player.furnace_burn_time = 45.0f;
                    app.player.furnace_max_burn_time = 80.0f;
                    app.player.furnace_cook_time = 6.2f;
                    app.player.inventory[0] = BLOCK_COBBLESTONE; app.player.inventory_counts[0] = 64;
                    app.player.inventory[1] = BLOCK_SAND;        app.player.inventory_counts[1] = 32;
                    app.player.hotbar[0] = ITEM_IRON_PICKAXE;    app.player.hotbar_counts[0] = 1;
                    app.player.hotbar[1] = ITEM_COOKED_BEEF;     app.player.hotbar_counts[1] = 16;
                    ui_set_screen(&app.ui, UI_SCREEN_FURNACE, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "chest")) {
                    app.player.creative = false;
                    app.player.chest[0] = ITEM_DIAMOND; app.player.chest_counts[0] = 12;
                    app.player.chest[1] = ITEM_GOLD_INGOT; app.player.chest_counts[1] = 24;
                    app.player.chest[2] = ITEM_IRON_INGOT; app.player.chest_counts[2] = 48;
                    app.player.chest[3] = ITEM_BREAD; app.player.chest_counts[3] = 16;
                    app.player.chest[4] = ITEM_APPLE; app.player.chest_counts[4] = 8;
                    app.player.inventory[0] = BLOCK_COBBLESTONE; app.player.inventory_counts[0] = 64;
                    app.player.hotbar[0] = ITEM_DIAMOND_PICKAXE; app.player.hotbar_counts[0] = 1;
                    app.player.hotbar[1] = ITEM_DIAMOND_SWORD; app.player.hotbar_counts[1] = 1;
                    ui_set_screen(&app.ui, UI_SCREEN_CHEST, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "hud_survival") ||
                           !strcmp(app.screenshot_screen, "hud")) {
                    app.player.creative = false;
                    app.player.health = 18.0f;
                    app.player.hunger = 19.0f;
                    app.player.armor_points = 14;
                    app.player.level = 27;
                    app.player.exp_progress = 0.65f;
                    ui_set_screen(&app.ui, UI_SCREEN_NONE, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "options")) {
                    ui_set_screen(&app.ui, UI_SCREEN_OPTIONS, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "video")) {
                    ui_set_screen(&app.ui, UI_SCREEN_VIDEO, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "audio")) {
                    ui_set_screen(&app.ui, UI_SCREEN_AUDIO, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "controls")) {
                    ui_set_screen(&app.ui, UI_SCREEN_CONTROLS, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "advancements")) {
                    ui_set_screen(&app.ui, UI_SCREEN_ADVANCEMENTS, app.has_save);
                } else if (!strcmp(app.screenshot_screen, "debug")) {
                    app.ui.show_debug = true;
                } else {
                    fprintf(stderr, "unknown screen: %s\n", app.screenshot_screen);
                    shutdown_app(&app);
                    return 1;
                }
            }
            Uint64 t0 = SDL_GetPerformanceCounter();
            Uint64 freq = SDL_GetPerformanceFrequency();
            for (int i = 0; i < app.screenshot_frames; i++) {
                update_playing(&app, 0.016f);
                render_frame(&app);
            }
            Uint64 t1 = SDL_GetPerformanceCounter();
            double elapsed = (double)(t1 - t0) / (double)freq;
            if (elapsed > 0.0001 && app.screenshot_frames >= 10) {
                printf("benchmark: %d frames in %.3fs (%.1f FPS, %.2f ms/frame, render_radius=%d chunks / %dm)\n",
                       app.screenshot_frames, elapsed,
                       (double)app.screenshot_frames / elapsed,
                       (elapsed / (double)app.screenshot_frames) * 1000.0,
                       app.renderer.render_radius,
                       app.renderer.render_radius * CHUNK_SIZE);
            }
        }
        bool ok = renderer_screenshot(&app.renderer, app.screenshot_path);
        shutdown_app(&app);
        return ok ? 0 : 1;
    }

    if (app.play_direct) {
        start_world(&app, !app.has_save);
    } else {
        ui_set_screen(&app.ui, UI_SCREEN_TITLE, app.has_save);
        audio_music_menu(&app.audio, true);
    }
    update_mouse_capture(&app);
    if (app.fullscreen)
        toggle_fullscreen(&app);

    Uint64 last = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    float fps_smooth = 60.0f;

    while (app.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            handle_event(&app, &e);

        if (!app.world_active &&
            app.ui.screen != UI_SCREEN_TITLE &&
            app.ui.screen != UI_SCREEN_CREATE_WORLD &&
            app.ui.screen != UI_SCREEN_OPTIONS &&
            app.ui.screen != UI_SCREEN_VIDEO &&
            app.ui.screen != UI_SCREEN_AUDIO &&
            app.ui.screen != UI_SCREEN_CONTROLS) {
            ui_set_screen(&app.ui, UI_SCREEN_TITLE, app.has_save);
        }

        if (app.ui.screen != UI_SCREEN_NONE && app.ui.screen != UI_SCREEN_LOADING &&
            !app.headless) {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            float fmx = (float)mx, fmy = (float)my;
#if MCED_USE_GL
            if (app.renderer.ui_scale > 0.0f) {
                fmx /= app.renderer.ui_scale;
                fmy /= app.renderer.ui_scale;
            }
#endif
            ui_mouse_move(&app.ui, fmx, fmy);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / (double)freq);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (dt < 0.0001f) dt = 0.0001f;
        fps_smooth = fps_smooth * 0.92f + (1.0f / dt) * 0.08f;
        app.fps = fps_smooth;

        if (app.world_active && (app.ui.screen == UI_SCREEN_NONE || app.ui.screen == UI_SCREEN_CHAT))
            update_playing(&app, dt);
        else if (app.toast_timer > 0)
            app.toast_timer -= dt;

        render_frame(&app);

#if !MCED_USE_GL
        if (!app.headless && app.sdl) {
            /* Keep the CPU budget sane: no busy-spin if vsync is unavailable. */
            uint32_t elapsed = (uint32_t)((double)(SDL_GetPerformanceCounter() - now) /
                                          (double)freq * 1000.0);
            if (elapsed < 4)
                SDL_Delay(4 - elapsed);
        }
#else
        if (app.max_fps > 0) {
            double target_s = 1.0 / (double)app.max_fps;
            double frame_s = (double)(SDL_GetPerformanceCounter() - now) / (double)freq;
            if (frame_s < target_s) {
                uint32_t delay_ms = (uint32_t)((target_s - frame_s) * 1000.0);
                if (delay_ms > 0) SDL_Delay(delay_ms);
            }
        }
#endif
    }

    save_world(&app, true);
    shutdown_app(&app);
    return 0;
}

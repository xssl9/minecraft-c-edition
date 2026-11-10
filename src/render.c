#include "render.h"

#include "blocks.h"
#include "thread_pool.h"

#include <math.h>
#include <pthread.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#include "stb_image.h"
#pragma GCC diagnostic pop

static GLuint load_panorama_texture(const char *path) {
    int w = 0, h = 0, ch = 0;
    uint8_t *data = stbi_load(path, &w, &h, &ch, 3);
    if (!data)
        return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
#if MCED_USE_GL
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
#endif
    stbi_image_free(data);
    return tex;
}

static GLuint load_texture_png(const char *path) {
    int w = 0, h = 0, ch = 0;
    uint8_t *data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) {
        fprintf(stderr, "renderer: missing texture %s\n", path);
        return 0;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if MCED_USE_GL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
#endif
    stbi_image_free(data);
    return tex;
}

static GLuint load_celestial_texture(const char *path) {
    int w = 0, h = 0, ch = 0;
    uint8_t *data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) {
        fprintf(stderr, "renderer: missing celestial texture %s\n", path);
        return 0;
    }
    /* Minecraft sun/moon textures have black (0,0,0) background.
       Derive alpha from max(R, max(G, B)) so transparent space has alpha 0
       and center shines with full opacity, matching Java Edition celestial rendering. */
    for (int i = 0; i < w * h; i++) {
        uint8_t r = data[i * 4 + 0];
        uint8_t g = data[i * 4 + 1];
        uint8_t b = data[i * 4 + 2];
        uint8_t mx = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
        data[i * 4 + 3] = mx;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#if MCED_USE_GL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
#endif
    stbi_image_free(data);
    return tex;
}

struct ChunkMesh {
    int cx, cz;
    RVertex *verts;
    int count;
    int capacity;
    int min_y, max_y;
    bool built;
    bool meshing;
    uint32_t version;
#if MCED_USE_GL
    GLuint list;
#endif
};

/* ------------------------------------------------------------------ */
/* TinyGL setup                                                        */
/* ------------------------------------------------------------------ */

static GLuint upload_rgb(const uint8_t *rgb) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if MCED_USE_GL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, MCED_ATLAS_SIZE, MCED_ATLAS_SIZE, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, (void *)rgb);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, 3, MCED_ATLAS_SIZE, MCED_ATLAS_SIZE, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, (void *)rgb);
#endif
    return tex;
}

static GLuint upload_rgba(const uint8_t *rgba) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if MCED_USE_GL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MCED_ATLAS_SIZE, MCED_ATLAS_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, (void *)rgba);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, 4, MCED_ATLAS_SIZE, MCED_ATLAS_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, (void *)rgba);
#endif
    return tex;
}

static void build_font_texture(const Assets *assets, uint8_t *out) {
    memset(out, 0, (size_t)MCED_ATLAS_SIZE * MCED_ATLAS_SIZE * 3);
    if (!assets->has_font)
        return;
    /* The vanilla font is 128x128, 16x16 cells of 8x8 pixels.  Expand it
       eight times to fill TinyGL's 1024x1024 texture, which keeps nearest
       sampling crisp without requiring alpha testing. */
    for (int sy = 0; sy < 128; sy++) {
        for (int sx = 0; sx < 128; sx++) {
            uint8_t a = assets->font_alpha[sy * 128 + sx];
            if (a < 24)
                continue;
            for (int py = 0; py < 8; py++) {
                uint8_t *row = out + (((size_t)(sy * 8 + py) * MCED_ATLAS_SIZE) +
                                      (sx * 8)) * 3;
                for (int px = 0; px < 8; px++) {
                    row[px * 3 + 0] = 255;
                    row[px * 3 + 1] = 255;
                    row[px * 3 + 2] = 255;
                }
            }
        }
    }
}

void renderer_resize(Renderer *r, int width, int height) {
    r->width = width & ~1;
    r->height = height;
    float scale = (float)height / 360.0f;
    if (scale < 1.0f) scale = 1.0f;
    r->ui_scale = scale;
    r->ui_width = (int)((float)r->width / scale);
    r->ui_height = (int)((float)r->height / scale);
    glViewport(0, 0, r->width, r->height);
}

bool renderer_init(Renderer *r, const Assets *assets, int width, int height) {
    memset(r, 0, sizeof(*r));
    r->assets = assets;
    r->render_radius = 16;
    r->scratch_verts_capacity = 65536;
    r->scratch_verts = xmalloc((size_t)r->scratch_verts_capacity * sizeof(RVertex));

    pthread_mutex_t *cmtx = malloc(sizeof(pthread_mutex_t));
    if (cmtx) {
        pthread_mutex_init(cmtx, NULL);
    }
    r->completed_mutex = cmtx;

    r->fog_color[0] = 0.72f;
    r->fog_color[1] = 0.84f;
    r->fog_color[2] = 1.0f;
    r->fog_color[3] = 1.0f;
    r->last_pcx = 999999;
    r->last_pcz = 999999;
    r->all_built = false;
    renderer_resize(r, width, height);

#if MCED_USE_GL
    glViewport(0, 0, r->width, r->height);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.55f, 0.72f, 0.98f, 1.0f);
#else
    r->zb = ZB_open(r->width, r->height, ZB_MODE_RGBA, NULL);
    if (!r->zb) {
        fprintf(stderr, "TinyGL: could not create framebuffer\n");
        return false;
    }
    glInit(r->zb);
    glViewport(0, 0, r->width, r->height);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.55f, 0.72f, 0.98f, 1.0f);
#endif

#if MCED_USE_GL
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
#endif

    if (assets->atlas_rgba)
        r->atlas_tex = upload_rgba(assets->atlas_rgba);
    else
        r->atlas_tex = upload_rgb(assets->atlas_rgb);
    if (assets->has_menu)
        r->menu_tex = upload_rgb(assets->menu_rgb);
    for (int i = 0; i < 6; i++) {
        char pano_path[1024];
        snprintf(pano_path, sizeof(pano_path),
                 "%s/minecraft/textures/gui/title/background/panorama_%d.png",
                 assets->root, i);
        r->panorama_tex[i] = load_panorama_texture(pano_path);
    }
    if (assets->has_logo)
        r->logo_tex = upload_rgb(assets->logo_rgb);

    uint8_t *font = xmalloc((size_t)MCED_ATLAS_SIZE * MCED_ATLAS_SIZE * 3);
    build_font_texture(assets, font);
    r->font_tex = upload_rgb(font);
    free(font);

    char env_path[1024];
    snprintf(env_path, sizeof(env_path), "%s/minecraft/textures/environment/sun.png", assets->root);
    r->sun_tex = load_celestial_texture(env_path);
    snprintf(env_path, sizeof(env_path), "%s/minecraft/textures/environment/moon_phases.png", assets->root);
    r->moon_tex = load_celestial_texture(env_path);
    snprintf(env_path, sizeof(env_path), "%s/minecraft/textures/environment/clouds.png", assets->root);
    r->clouds_tex = load_texture_png(env_path);

    r->fov = 70.0f;
    r->clouds_enabled = true;
    r->fog_enabled = true;
    r->view_bobbing = true;
    r->brightness = 0.5f;

    char tex_path[1024];
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/widget/button.png", assets->root);
    r->button_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/widget/button_highlighted.png", assets->root);
    r->button_hl_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/widget/button_disabled.png", assets->root);
    r->button_disabled_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/widget/slider.png", assets->root);
    r->slider_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/widget/slider_highlighted.png", assets->root);
    r->slider_hl_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/widget/slider_handle.png", assets->root);
    r->slider_handle_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/widget/slider_handle_highlighted.png", assets->root);
    r->slider_handle_hl_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/block/dirt.png", assets->root);
    r->dirt_bg_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/advancements/window.png", assets->root);
    r->adv_window_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/advancements/backgrounds/stone.png", assets->root);
    r->adv_stone_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/entity/player/wide/steve.png", assets->root);
    r->steve_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/entity/zombie/zombie.png", assets->root);
    r->mob_zombie_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/entity/skeleton/skeleton.png", assets->root);
    r->mob_skeleton_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/entity/creeper/creeper.png", assets->root);
    r->mob_creeper_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/entity/pig/pig.png", assets->root);
    r->mob_pig_tex = load_texture_png(tex_path);

    /* Authentic HUD textures */
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/hotbar.png", assets->root);
    r->gui_hotbar_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/hotbar_selection.png", assets->root);
    r->gui_hotbar_sel_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/crosshair.png", assets->root);
    r->gui_crosshair_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/experience_bar_background.png", assets->root);
    r->gui_exp_bg_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/experience_bar_progress.png", assets->root);
    r->gui_exp_prog_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/heart/container.png", assets->root);
    r->gui_heart_empty_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/heart/full.png", assets->root);
    r->gui_heart_full_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/heart/half.png", assets->root);
    r->gui_heart_half_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/heart/full_blinking.png", assets->root);
    r->gui_heart_blink_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/heart/half_blinking.png", assets->root);
    r->gui_heart_half_blink_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/food_empty.png", assets->root);
    r->gui_food_empty_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/food_full.png", assets->root);
    r->gui_food_full_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/food_half.png", assets->root);
    r->gui_food_half_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/armor_empty.png", assets->root);
    r->gui_armor_empty_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/armor_full.png", assets->root);
    r->gui_armor_full_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/armor_half.png", assets->root);
    r->gui_armor_half_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/air.png", assets->root);
    r->gui_air_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/hud/air_bursting.png", assets->root);
    r->gui_air_pop_tex = load_texture_png(tex_path);

    /* Containers */
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/container/inventory.png", assets->root);
    r->gui_inventory_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/container/creative_inventory/tab_items.png", assets->root);
    r->gui_creative_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/container/crafting_table.png", assets->root);
    r->gui_crafting_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/container/furnace.png", assets->root);
    r->gui_furnace_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/container/furnace/lit_progress.png", assets->root);
    r->gui_furnace_lit_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/sprites/container/furnace/burn_progress.png", assets->root);
    r->gui_furnace_burn_tex = load_texture_png(tex_path);
    snprintf(tex_path, sizeof(tex_path), "%s/minecraft/textures/gui/container/generic_54.png", assets->root);
    r->gui_chest_tex = load_texture_png(tex_path);

    for (int i = 0; i < 10; i++) {
        char name[64];
        snprintf(name, sizeof(name), "block/destroy_stage_%d", i);
        int t = assets_tile(assets, name);
        r->break_stage_tiles[i] = (t >= 0) ? (uint16_t)t : (uint16_t)(321 + i);
    }

#if MCED_USE_GL
    printf("renderer: Hardware OpenGL %dx%d (UI %.1fx, %dx%d), atlas tex %u, font tex %u\n",
           r->width, r->height, r->ui_scale, r->ui_width, r->ui_height, r->atlas_tex, r->font_tex);
#else
    printf("renderer: TinyGL %dx%d, atlas tex %u, font tex %u\n",
           r->width, r->height, r->atlas_tex, r->font_tex);
#endif
    return true;
}

void renderer_set_thread_pool(Renderer *r, struct ThreadPool *pool) {
    r->pool = pool;
}

void renderer_shutdown(Renderer *r) {
    renderer_reset_chunks(r);
    if (r->completed_mutex) {
        pthread_mutex_destroy((pthread_mutex_t *)r->completed_mutex);
        free(r->completed_mutex);
        r->completed_mutex = NULL;
    }
    free(r->completed_meshes);
    r->completed_meshes = NULL;
    r->completed_count = 0;
    r->completed_capacity = 0;
    free(r->chunks);
    r->chunks = NULL;
    free(r->mesh_hash);
    r->mesh_hash = NULL;
    free(r->scratch_verts);
    r->scratch_verts = NULL;
    if (r->atlas_tex) glDeleteTextures(1, &r->atlas_tex);
    if (r->menu_tex) glDeleteTextures(1, &r->menu_tex);
    for (int i = 0; i < 6; i++) {
        if (r->panorama_tex[i]) glDeleteTextures(1, &r->panorama_tex[i]);
    }
    if (r->logo_tex) glDeleteTextures(1, &r->logo_tex);
    if (r->font_tex) glDeleteTextures(1, &r->font_tex);
    if (r->sun_tex) glDeleteTextures(1, &r->sun_tex);
    if (r->moon_tex) glDeleteTextures(1, &r->moon_tex);
    if (r->clouds_tex) glDeleteTextures(1, &r->clouds_tex);
    if (r->button_tex) glDeleteTextures(1, &r->button_tex);
    if (r->button_hl_tex) glDeleteTextures(1, &r->button_hl_tex);
    if (r->button_disabled_tex) glDeleteTextures(1, &r->button_disabled_tex);
    if (r->slider_tex) glDeleteTextures(1, &r->slider_tex);
    if (r->slider_hl_tex) glDeleteTextures(1, &r->slider_hl_tex);
    if (r->slider_handle_tex) glDeleteTextures(1, &r->slider_handle_tex);
    if (r->slider_handle_hl_tex) glDeleteTextures(1, &r->slider_handle_hl_tex);
    if (r->dirt_bg_tex) glDeleteTextures(1, &r->dirt_bg_tex);
    if (r->adv_window_tex) glDeleteTextures(1, &r->adv_window_tex);
    if (r->adv_stone_tex) glDeleteTextures(1, &r->adv_stone_tex);
    if (r->steve_tex) glDeleteTextures(1, &r->steve_tex);
    if (r->mob_zombie_tex) glDeleteTextures(1, &r->mob_zombie_tex);
    if (r->mob_skeleton_tex) glDeleteTextures(1, &r->mob_skeleton_tex);
    if (r->mob_creeper_tex) glDeleteTextures(1, &r->mob_creeper_tex);
    if (r->mob_pig_tex) glDeleteTextures(1, &r->mob_pig_tex);
    if (r->gui_hotbar_tex) glDeleteTextures(1, &r->gui_hotbar_tex);
    if (r->gui_hotbar_sel_tex) glDeleteTextures(1, &r->gui_hotbar_sel_tex);
    if (r->gui_crosshair_tex) glDeleteTextures(1, &r->gui_crosshair_tex);
    if (r->gui_exp_bg_tex) glDeleteTextures(1, &r->gui_exp_bg_tex);
    if (r->gui_exp_prog_tex) glDeleteTextures(1, &r->gui_exp_prog_tex);
    if (r->gui_heart_empty_tex) glDeleteTextures(1, &r->gui_heart_empty_tex);
    if (r->gui_heart_full_tex) glDeleteTextures(1, &r->gui_heart_full_tex);
    if (r->gui_heart_half_tex) glDeleteTextures(1, &r->gui_heart_half_tex);
    if (r->gui_heart_blink_tex) glDeleteTextures(1, &r->gui_heart_blink_tex);
    if (r->gui_heart_half_blink_tex) glDeleteTextures(1, &r->gui_heart_half_blink_tex);
    if (r->gui_food_empty_tex) glDeleteTextures(1, &r->gui_food_empty_tex);
    if (r->gui_food_full_tex) glDeleteTextures(1, &r->gui_food_full_tex);
    if (r->gui_food_half_tex) glDeleteTextures(1, &r->gui_food_half_tex);
    if (r->gui_armor_empty_tex) glDeleteTextures(1, &r->gui_armor_empty_tex);
    if (r->gui_armor_full_tex) glDeleteTextures(1, &r->gui_armor_full_tex);
    if (r->gui_armor_half_tex) glDeleteTextures(1, &r->gui_armor_half_tex);
    if (r->gui_air_tex) glDeleteTextures(1, &r->gui_air_tex);
    if (r->gui_air_pop_tex) glDeleteTextures(1, &r->gui_air_pop_tex);
    if (r->gui_inventory_tex) glDeleteTextures(1, &r->gui_inventory_tex);
    if (r->gui_creative_tex) glDeleteTextures(1, &r->gui_creative_tex);
    if (r->gui_crafting_tex) glDeleteTextures(1, &r->gui_crafting_tex);
    if (r->gui_furnace_tex) glDeleteTextures(1, &r->gui_furnace_tex);
    if (r->gui_furnace_lit_tex) glDeleteTextures(1, &r->gui_furnace_lit_tex);
    if (r->gui_furnace_burn_tex) glDeleteTextures(1, &r->gui_furnace_burn_tex);
    if (r->gui_chest_tex) glDeleteTextures(1, &r->gui_chest_tex);
#if !MCED_USE_GL
    if (r->zb) {
        glClose();
        ZB_close(r->zb);
    }
#endif
    memset(r, 0, sizeof(*r));
}

/* ------------------------------------------------------------------ */
/* Matrices / screen helpers                                           */
/* ------------------------------------------------------------------ */

static void set_ortho(Renderer *r) {
    float w = (float)(r->ui_width > 0 ? r->ui_width : r->width);
    float h = (float)(r->ui_height > 0 ? r->ui_height : r->height);
    float m[16] = {
        2.0f / w, 0, 0, 0,
        0, -2.0f / h, 0, 0,
        0, 0, 1, 0,
        -1.0f, 1.0f, 0, 1
    };
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void set_perspective(Renderer *r, Vec3 eye, float yaw, float pitch) {
    float aspect = (float)r->width / (float)r->height;
    float near = 0.05f;
    float far = (float)((r->render_radius + 4) * CHUNK_SIZE);
    if (far < 500.0f) far = 500.0f;
    if (far > 3000.0f) far = 3000.0f;
    float fov_deg = r->fov > 20.0f ? r->fov : 70.0f;
    float fov = fov_deg * MCED_DEG2RAD;
    float nh = tanf(fov * 0.5f) * near;
    float nw = nh * aspect;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-nw, nw, -nh, nh, near, far);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* TinyGL's glRotatef is the inverse of the standard GL rotation for
       these axes.  Negate both angles so the rendered view direction
       (-sin(yaw)*cos(pitch), sin(pitch), -cos(yaw)*cos(pitch)) matches
       player_forward(), the raycast, and the movement basis. */
    glRotatef(-pitch, 1, 0, 0);
    glRotatef(-yaw, 0, 1, 0);
    glTranslatef(-eye.x, -eye.y, -eye.z);
}

void renderer_clear(Renderer *r) {
    glViewport(0, 0, r->width, r->height);
    glClearColor(0.55f, 0.72f, 0.98f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void renderer_sky(Renderer *r, float world_time, Vec3 eye, float yaw, float pitch) {
    (void)eye;
    glViewport(0, 0, r->width, r->height);

    if (r->submerged_fluid == 1) {
        /* Submerged in water: deep aquatic blue abyss */
        r->fog_color[0] = 0.03f;
        r->fog_color[1] = 0.12f;
        r->fog_color[2] = 0.28f;
        r->fog_color[3] = 1.0f;
        glClearColor(0.02f, 0.08f, 0.22f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }
    if (r->submerged_fluid == 2) {
        /* Submerged in lava: thick fiery orange-red magma */
        r->fog_color[0] = 0.65f;
        r->fog_color[1] = 0.14f;
        r->fog_color[2] = 0.01f;
        r->fog_color[3] = 1.0f;
        glClearColor(0.55f, 0.10f, 0.00f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    /* Day cycle: 1200 seconds = 20 minutes (vanilla Minecraft day) */
    /* 0s = 06:00 (sunrise), 300s = 12:00 (noon), 600s = 18:00 (sunset), 900s = 00:00 (midnight) */
    float cycle = fmodf(world_time, 1200.0f);
    if (cycle < 0.0f) cycle += 1200.0f;

    /* Celestial rotation angle:
       At 300s (Noon): angle = 0.0 rad (0 deg), sun at +Y zenith.
       At 600s (Sunset): angle = PI/2 rad (90 deg), sun at -X west horizon.
       At 900s (Midnight): angle = PI rad (180 deg), sun at -Y nadir, moon at +Y zenith.
       At 0s (Sunrise): angle = -PI/2 rad (-90 deg), sun at +X east horizon. */
    float celestial_angle = ((cycle - 300.0f) / 1200.0f) * 2.0f * (float)M_PI;
    float sun_elev = cosf(celestial_angle);
    float celestial_deg = celestial_angle * (180.0f / (float)M_PI);

    /* Dynamic sky and horizon colors */
    float sky_r, sky_g, sky_b;
    float horiz_r, horiz_g, horiz_b;

    if (sun_elev > 0.20f) {
        /* Full daylight - vibrant classic Minecraft sky */
        float t = fminf(1.0f, (sun_elev - 0.20f) / 0.60f);
        sky_r = 0.38f + 0.04f * t;
        sky_g = 0.60f + 0.06f * t;
        sky_b = 0.98f;
        horiz_r = 0.72f + 0.06f * t;
        horiz_g = 0.84f + 0.04f * t;
        horiz_b = 1.0f;
    } else if (sun_elev > -0.20f) {
        /* Sunrise / Sunset transition */
        float t = (sun_elev + 0.20f) / 0.40f; /* 0.0 = night, 1.0 = day */
        float sunset_w = 1.0f - fabsf(t - 0.5f) * 2.0f;
        if (sunset_w < 0.0f) sunset_w = 0.0f;

        float day_sky_r = 0.38f, day_sky_g = 0.60f, day_sky_b = 0.98f;
        float night_sky_r = 0.015f, night_sky_g = 0.02f, night_sky_b = 0.045f;
        sky_r = night_sky_r + (day_sky_r - night_sky_r) * t;
        sky_g = night_sky_g + (day_sky_g - night_sky_g) * t;
        sky_b = night_sky_b + (day_sky_b - night_sky_b) * t;

        float day_hz_r = 0.72f, day_hz_g = 0.84f, day_hz_b = 1.0f;
        float night_hz_r = 0.04f, night_hz_g = 0.05f, night_hz_b = 0.08f;
        float base_hz_r = night_hz_r + (day_hz_r - night_hz_r) * t;
        float base_hz_g = night_hz_g + (day_hz_g - night_hz_g) * t;
        float base_hz_b = night_hz_b + (day_hz_b - night_hz_b) * t;

        /* Warm golden orange sunset glow */
        horiz_r = base_hz_r * (1.0f - sunset_w) + 0.98f * sunset_w;
        horiz_g = base_hz_g * (1.0f - sunset_w) + 0.54f * sunset_w;
        horiz_b = base_hz_b * (1.0f - sunset_w) + 0.22f * sunset_w;
    } else {
        /* Night sky */
        sky_r = 0.015f; sky_g = 0.02f; sky_b = 0.045f;
        horiz_r = 0.04f; horiz_g = 0.05f; horiz_b = 0.08f;
    }

    /* Update horizon fog color for authentic distance blending */
    r->fog_color[0] = horiz_r;
    r->fog_color[1] = horiz_g;
    r->fog_color[2] = horiz_b;
    r->fog_color[3] = 1.0f;

    glClearColor(sky_r, sky_g, sky_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* 3D camera-relative projection (rotation only, NO eye translation) */
    float aspect = (float)r->width / (float)r->height;
    float near = 0.05f, far = 2000.0f;
    float fov_deg = r->fov > 20.0f ? r->fov : 70.0f;
    float fov = fov_deg * MCED_DEG2RAD;
    float nh = tanf(fov * 0.5f) * near;
    float nw = nh * aspect;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-nw, nw, -nh, nh, near, far);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-pitch, 1, 0, 0);
    glRotatef(-yaw, 0, 1, 0);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);

    /* 1. Seamless 360-degree Sky Dome */
    float dome_r = 120.0f;
    int segments = 24;

    /* Upper hemisphere: zenith fan */
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(sky_r, sky_g, sky_b);
    glVertex3f(0.0f, dome_r, 0.0f);
    for (int i = 0; i <= segments; i++) {
        float a = (float)i * 2.0f * (float)M_PI / (float)segments;
        glColor3f(horiz_r, horiz_g, horiz_b);
        glVertex3f(cosf(a) * dome_r, 4.0f, sinf(a) * dome_r);
    }
    glEnd();

    /* Horizon to lower fog band */
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= segments; i++) {
        float a = (float)i * 2.0f * (float)M_PI / (float)segments;
        glColor3f(horiz_r, horiz_g, horiz_b);
        glVertex3f(cosf(a) * dome_r, 4.0f, sinf(a) * dome_r);
        glColor3f(horiz_r * 0.35f, horiz_g * 0.35f, horiz_b * 0.35f);
        glVertex3f(cosf(a) * dome_r, -30.0f, sinf(a) * dome_r);
    }
    glEnd();

    /* Lower hemisphere / void bottom */
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(horiz_r * 0.15f, horiz_g * 0.15f, horiz_b * 0.15f);
    glVertex3f(0.0f, -dome_r, 0.0f);
    for (int i = 0; i <= segments; i++) {
        float a = (float)i * 2.0f * (float)M_PI / (float)segments;
        glColor3f(horiz_r * 0.35f, horiz_g * 0.35f, horiz_b * 0.35f);
        glVertex3f(cosf(a) * dome_r, -30.0f, sinf(a) * dome_r);
    }
    glEnd();

    /* 2. Celestial sphere (Sun, Moon, Stars) */
    glPushMatrix();
    /* Rotate celestial sphere around Z axis */
    glRotatef(celestial_deg, 0.0f, 0.0f, 1.0f);

    /* Stars: visible when sun is low or below horizon */
    if (sun_elev < 0.15f) {
        float star_alpha = fminf(1.0f, (0.15f - sun_elev) * 4.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(1.0f, 1.0f, 1.0f, star_alpha);
        glBegin(GL_QUADS);
        uint32_t s_rng = 0x12345678U;
        for (int i = 0; i < 200; i++) {
            s_rng = mced_hash_u32(s_rng + 0x9e3779b9U);
            float u = (float)(s_rng & 1023) / 1023.0f * 2.0f - 1.0f;
            s_rng = mced_hash_u32(s_rng + 0x9e3779b9U);
            float v = (float)(s_rng & 1023) / 1023.0f * (float)M_PI * 2.0f;
            float r_xy = sqrtf(fmaxf(0.0f, 1.0f - u * u));
            float px = r_xy * cosf(v) * 95.0f;
            float py = fabsf(u) * 95.0f;
            float pz = r_xy * sinf(v) * 95.0f;
            float ss = 0.35f;
            glVertex3f(px - ss, py, pz - ss);
            glVertex3f(px + ss, py, pz - ss);
            glVertex3f(px + ss, py, pz + ss);
            glVertex3f(px - ss, py, pz + ss);
        }
        glEnd();
        glDisable(GL_BLEND);
    }

    /* Sun & Moon (additive blending like Minecraft Java Edition) */
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    /* Sun: quad at Y = +100 */
    if (r->sun_tex) {
        glBindTexture(GL_TEXTURE_2D, r->sun_tex);
        float s = 18.0f;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-s, 100.0f, -s);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( s, 100.0f, -s);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( s, 100.0f,  s);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-s, 100.0f,  s);
        glEnd();
    }

    /* Moon: quad at Y = -100 */
    if (r->moon_tex) {
        glBindTexture(GL_TEXTURE_2D, r->moon_tex);
        float ms = 14.0f;
        float u0 = 0.0f, u1 = 0.25f, v0 = 0.0f, v1 = 0.5f;
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex3f(-ms, -100.0f, -ms);
        glTexCoord2f(u1, v0); glVertex3f( ms, -100.0f, -ms);
        glTexCoord2f(u1, v1); glVertex3f( ms, -100.0f,  ms);
        glTexCoord2f(u0, v1); glVertex3f(-ms, -100.0f,  ms);
        glEnd();
    }

    glPopMatrix();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void renderer_clouds(Renderer *r, float world_time, Vec3 eye) {
    if (!r->clouds_enabled || !r->clouds_tex)
        return;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, r->clouds_tex);

    float cloud_y = 192.0f;
    float cloud_offset = world_time * 0.8f;
    float span = fmaxf(512.0f, (float)(r->render_radius + 4) * CHUNK_SIZE);
    float x0 = eye.x - span, x1 = eye.x + span;
    float z0 = eye.z - span, z1 = eye.z + span;

    /* Vanilla Minecraft repeat scale: 3072 blocks */
    float u0 = (x0 + cloud_offset) / 3072.0f;
    float u1 = (x1 + cloud_offset) / 3072.0f;
    float v0 = z0 / 3072.0f;
    float v1 = z1 / 3072.0f;

    glColor4f(1.0f, 1.0f, 1.0f, 0.82f);
    glBegin(GL_QUADS);
    /* Bottom face */
    glTexCoord2f(u0, v0); glVertex3f(x0, cloud_y, z0);
    glTexCoord2f(u0, v1); glVertex3f(x0, cloud_y, z1);
    glTexCoord2f(u1, v1); glVertex3f(x1, cloud_y, z1);
    glTexCoord2f(u1, v0); glVertex3f(x1, cloud_y, z0);

    /* Top face */
    glTexCoord2f(u0, v0); glVertex3f(x0, cloud_y + 4.0f, z1);
    glTexCoord2f(u0, v1); glVertex3f(x0, cloud_y + 4.0f, z0);
    glTexCoord2f(u1, v1); glVertex3f(x1, cloud_y + 4.0f, z0);
    glTexCoord2f(u1, v0); glVertex3f(x1, cloud_y + 4.0f, z1);
    glEnd();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void renderer_ui_begin(Renderer *r) {
    set_ortho(r);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glShadeModel(GL_SMOOTH);
    glColor3f(1, 1, 1);
}

/* ------------------------------------------------------------------ */
/* Chunk meshing                                                       */
/* ------------------------------------------------------------------ */

static void mesh_hash_insert(Renderer *r, ChunkMesh *m) {
    if (!r->mesh_hash || r->mesh_hash_capacity == 0)
        return;
    int mask = r->mesh_hash_capacity - 1;
    uint32_t slot = chunk_hash_key(m->cx, m->cz) & (uint32_t)mask;
    while (r->mesh_hash[slot])
        slot = (slot + 1) & (uint32_t)mask;
    r->mesh_hash[slot] = m;
}

static void mesh_hash_rebuild(Renderer *r) {
    int cap = 64;
    while (cap < r->chunk_count * 4)
        cap *= 2;
    free(r->mesh_hash);
    r->mesh_hash = xcalloc((size_t)cap, sizeof(ChunkMesh *));
    r->mesh_hash_capacity = cap;
    for (int i = 0; i < r->chunk_count; i++)
        mesh_hash_insert(r, &r->chunks[i]);
}

static ChunkMesh *mesh_find(Renderer *r, int cx, int cz) {
    if (r->mesh_hash_capacity > 0) {
        int mask = r->mesh_hash_capacity - 1;
        uint32_t slot = chunk_hash_key(cx, cz) & (uint32_t)mask;
        for (int probes = 0; probes < r->mesh_hash_capacity; probes++) {
            ChunkMesh *m = r->mesh_hash[slot];
            if (!m)
                return NULL;
            if (m->cx == cx && m->cz == cz)
                return m;
            slot = (slot + 1) & (uint32_t)mask;
        }
    }
    return NULL;
}

static ChunkMesh *mesh_get(Renderer *r, int cx, int cz) {
    ChunkMesh *m = mesh_find(r, cx, cz);
    if (m)
        return m;
    if (r->chunk_count == r->chunk_capacity) {
        r->chunk_capacity = r->chunk_capacity ? r->chunk_capacity * 2 : 64;
        r->chunks = xrealloc(r->chunks, (size_t)r->chunk_capacity * sizeof(ChunkMesh));
        mesh_hash_rebuild(r);
    }
    m = &r->chunks[r->chunk_count++];
    memset(m, 0, sizeof(*m));
    m->cx = cx;
    m->cz = cz;
    if (r->chunk_count * 2 >= r->mesh_hash_capacity)
        mesh_hash_rebuild(r);
    else
        mesh_hash_insert(r, m);
    return m;
}

typedef struct {
    RVertex *verts;
    int count;
    int capacity;
} MeshBuffer;

static inline void mesh_buf_push(MeshBuffer *mb, RVertex a, RVertex b, RVertex c, RVertex d) {
    if (mb->count + 4 > mb->capacity) {
        mb->capacity = mb->capacity ? mb->capacity * 2 : 16384;
        mb->verts = xrealloc(mb->verts, (size_t)mb->capacity * sizeof(RVertex));
    }
    mb->verts[mb->count++] = a;
    mb->verts[mb->count++] = b;
    mb->verts[mb->count++] = c;
    mb->verts[mb->count++] = d;
}

static const float AO_LUT[8] = {
    1.00f, 0.85f, 0.85f, 0.70f,
    0.85f, 0.70f, 0.50f, 0.50f
};

static inline float vertex_ao(bool s1, bool s2, bool c) {
    int idx = ((int)s1 << 2) | ((int)s2 << 1) | (int)c;
    return AO_LUT[idx];
}

static void vert(RVertex *v, float x, float y, float z, float u, float vv,
                 float sh, float tr, float tg, float tb) {
    v->x = x; v->y = y; v->z = z;
    v->u = u; v->v = vv;
    v->r = sh * tr;
    v->g = sh * tg;
    v->b = sh * tb;
}

static void emit_face(MeshBuffer *mb, const Assets *assets, int face, uint16_t tile,
                      float x, float y, float z, float h,
                      float sh, float tr, float tg, float tb,
                      const float ao[4]) {
    float u0, v0, u1, v1;
    assets_tile_uv(assets, tile, &u0, &v0, &u1, &v1);
    if (h < 0.99f && (face == FACE_NORTH || face == FACE_SOUTH || face == FACE_EAST || face == FACE_WEST)) {
        v1 = v0 + (v1 - v0) * h;
    }
    RVertex a, b, c, d;
    float sh0 = sh * (ao ? ao[0] : 1.0f);
    float sh1 = sh * (ao ? ao[1] : 1.0f);
    float sh2 = sh * (ao ? ao[2] : 1.0f);
    float sh3 = sh * (ao ? ao[3] : 1.0f);
    switch (face) {
    case FACE_TOP:
        vert(&a, x, y + h, z, u0, v0, sh0, tr, tg, tb);
        vert(&b, x, y + h, z + 1, u0, v1, sh1, tr, tg, tb);
        vert(&c, x + 1, y + h, z + 1, u1, v1, sh2, tr, tg, tb);
        vert(&d, x + 1, y + h, z, u1, v0, sh3, tr, tg, tb);
        break;
    case FACE_BOTTOM:
        vert(&a, x, y, z, u0, v1, sh0, tr, tg, tb);
        vert(&b, x + 1, y, z, u1, v1, sh1, tr, tg, tb);
        vert(&c, x + 1, y, z + 1, u1, v0, sh2, tr, tg, tb);
        vert(&d, x, y, z + 1, u0, v0, sh3, tr, tg, tb);
        break;
    case FACE_NORTH:
        vert(&a, x + 1, y + h, z, u0, v0, sh0, tr, tg, tb);
        vert(&b, x + 1, y, z, u0, v1, sh1, tr, tg, tb);
        vert(&c, x, y, z, u1, v1, sh2, tr, tg, tb);
        vert(&d, x, y + h, z, u1, v0, sh3, tr, tg, tb);
        break;
    case FACE_SOUTH:
        vert(&a, x, y + h, z + 1, u0, v0, sh0, tr, tg, tb);
        vert(&b, x, y, z + 1, u0, v1, sh1, tr, tg, tb);
        vert(&c, x + 1, y, z + 1, u1, v1, sh2, tr, tg, tb);
        vert(&d, x + 1, y + h, z + 1, u1, v0, sh3, tr, tg, tb);
        break;
    case FACE_EAST:
        vert(&a, x + 1, y + h, z + 1, u0, v0, sh0, tr, tg, tb);
        vert(&b, x + 1, y, z + 1, u0, v1, sh1, tr, tg, tb);
        vert(&c, x + 1, y, z, u1, v1, sh2, tr, tg, tb);
        vert(&d, x + 1, y + h, z, u1, v0, sh3, tr, tg, tb);
        break;
    case FACE_WEST:
        vert(&a, x, y + h, z, u0, v0, sh0, tr, tg, tb);
        vert(&b, x, y, z, u0, v1, sh1, tr, tg, tb);
        vert(&c, x, y, z + 1, u1, v1, sh2, tr, tg, tb);
        vert(&d, x, y + h, z + 1, u1, v0, sh3, tr, tg, tb);
        break;
    default:
        return;
    }
    mesh_buf_push(mb, a, b, c, d);
}

static void emit_cross(MeshBuffer *mb, const Assets *assets, uint16_t tile,
                       float x, float y, float z, float h, float sh, float tr, float tg, float tb) {
    float u0, v0, u1, v1;
    assets_tile_uv(assets, tile, &u0, &v0, &u1, &v1);
    RVertex a, b, c, d;
    /* Diagonal 1 */
    vert(&a, x + 0.15f, y + h, z + 0.15f, u0, v0, sh, tr, tg, tb);
    vert(&b, x + 0.15f, y, z + 0.15f, u0, v1, sh, tr, tg, tb);
    vert(&c, x + 0.85f, y, z + 0.85f, u1, v1, sh, tr, tg, tb);
    vert(&d, x + 0.85f, y + h, z + 0.85f, u1, v0, sh, tr, tg, tb);
    mesh_buf_push(mb, a, b, c, d);
    vert(&a, x + 0.85f, y + h, z + 0.85f, u1, v0, sh, tr, tg, tb);
    vert(&b, x + 0.85f, y, z + 0.85f, u1, v1, sh, tr, tg, tb);
    vert(&c, x + 0.15f, y, z + 0.15f, u0, v1, sh, tr, tg, tb);
    vert(&d, x + 0.15f, y + h, z + 0.15f, u0, v0, sh, tr, tg, tb);
    mesh_buf_push(mb, a, b, c, d);

    /* Diagonal 2 */
    vert(&a, x + 0.15f, y + h, z + 0.85f, u0, v0, sh, tr, tg, tb);
    vert(&b, x + 0.15f, y, z + 0.85f, u0, v1, sh, tr, tg, tb);
    vert(&c, x + 0.85f, y, z + 0.15f, u1, v1, sh, tr, tg, tb);
    vert(&d, x + 0.85f, y + h, z + 0.15f, u1, v0, sh, tr, tg, tb);
    mesh_buf_push(mb, a, b, c, d);
    vert(&a, x + 0.85f, y + h, z + 0.85f, u1, v0, sh, tr, tg, tb);
    vert(&b, x + 0.85f, y, z + 0.15f, u1, v1, sh, tr, tg, tb);
    vert(&c, x + 0.15f, y, z + 0.85f, u0, v1, sh, tr, tg, tb);
    vert(&d, x + 0.15f, y + h, z + 0.85f, u0, v0, sh, tr, tg, tb);
    mesh_buf_push(mb, a, b, c, d);
}

static void emit_flat_floor(MeshBuffer *mb, const Assets *assets, uint16_t tile,
                            float x, float y, float z, float sh, float tr, float tg, float tb) {
    float u0, v0, u1, v1;
    assets_tile_uv(assets, tile, &u0, &v0, &u1, &v1);
    RVertex a, b, c, d;
    float y_flat = y + 0.02f;
    vert(&a, x, y_flat, z, u0, v0, sh, tr, tg, tb);
    vert(&b, x, y_flat, z + 1, u0, v1, sh, tr, tg, tb);
    vert(&c, x + 1, y_flat, z + 1, u1, v1, sh, tr, tg, tb);
    vert(&d, x + 1, y_flat, z, u1, v0, sh, tr, tg, tb);
    mesh_buf_push(mb, a, b, c, d);
}

static void emit_door(MeshBuffer *mb, const Assets *assets, uint8_t id, bool is_top,
                      float x, float y, float z, float light) {
    bool open = (id == BLOCK_OAK_DOOR_OPEN || id == BLOCK_IRON_DOOR_OPEN);
    bool iron = (id == BLOCK_IRON_DOOR || id == BLOCK_IRON_DOOR_OPEN);
    const char *tex_name;
    if (iron) {
        tex_name = is_top ? "block/iron_door_top" : "block/iron_door_bottom";
    } else {
        tex_name = is_top ? "block/oak_door_top" : "block/oak_door_bottom";
    }
    int tile = assets_tile(assets, tex_name);
    if (tile < 0) tile = g_block_tiles[id][FACE_NORTH];
    float u0, v0, u1, v1;
    assets_tile_uv(assets, (uint16_t)tile, &u0, &v0, &u1, &v1);

    float t = 0.1875f;
    RVertex a, b, c, d;
    float sh = 0.85f * light;
    float edge_sh = 0.65f * light;
    float top_sh = 1.0f * light;

    if (!open) {
        /* South face (front) */
        vert(&a, x, y + 1.0f, z + t, u0, v0, sh, 1, 1, 1);
        vert(&b, x, y, z + t, u0, v1, sh, 1, 1, 1);
        vert(&c, x + 1.0f, y, z + t, u1, v1, sh, 1, 1, 1);
        vert(&d, x + 1.0f, y + 1.0f, z + t, u1, v0, sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* North face (back) */
        vert(&a, x + 1.0f, y + 1.0f, z, u0, v0, sh, 1, 1, 1);
        vert(&b, x + 1.0f, y, z, u0, v1, sh, 1, 1, 1);
        vert(&c, x, y, z, u1, v1, sh, 1, 1, 1);
        vert(&d, x, y + 1.0f, z, u1, v0, sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* West edge */
        vert(&a, x, y + 1.0f, z, u0, v0, edge_sh, 1, 1, 1);
        vert(&b, x, y, z, u0, v1, edge_sh, 1, 1, 1);
        vert(&c, x, y, z + t, u1, v1, edge_sh, 1, 1, 1);
        vert(&d, x, y + 1.0f, z + t, u1, v0, edge_sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* East edge */
        vert(&a, x + 1.0f, y + 1.0f, z + t, u0, v0, edge_sh, 1, 1, 1);
        vert(&b, x + 1.0f, y, z + t, u0, v1, edge_sh, 1, 1, 1);
        vert(&c, x + 1.0f, y, z, u1, v1, edge_sh, 1, 1, 1);
        vert(&d, x + 1.0f, y + 1.0f, z, u1, v0, edge_sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* Top edge */
        vert(&a, x, y + 1.0f, z, u0, v0, top_sh, 1, 1, 1);
        vert(&b, x, y + 1.0f, z + t, u0, v1, top_sh, 1, 1, 1);
        vert(&c, x + 1.0f, y + 1.0f, z + t, u1, v1, top_sh, 1, 1, 1);
        vert(&d, x + 1.0f, y + 1.0f, z, u1, v0, top_sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);
    } else {
        /* Open door: rotated 90 degrees along X axis */
        /* East face */
        vert(&a, x + t, y + 1.0f, z + 1.0f, u0, v0, sh, 1, 1, 1);
        vert(&b, x + t, y, z + 1.0f, u0, v1, sh, 1, 1, 1);
        vert(&c, x + t, y, z, u1, v1, sh, 1, 1, 1);
        vert(&d, x + t, y + 1.0f, z, u1, v0, sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* West face */
        vert(&a, x, y + 1.0f, z, u0, v0, sh, 1, 1, 1);
        vert(&b, x, y, z, u0, v1, sh, 1, 1, 1);
        vert(&c, x, y, z + 1.0f, u1, v1, sh, 1, 1, 1);
        vert(&d, x, y + 1.0f, z + 1.0f, u1, v0, sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* North edge */
        vert(&a, x + t, y + 1.0f, z, u0, v0, edge_sh, 1, 1, 1);
        vert(&b, x + t, y, z, u0, v1, edge_sh, 1, 1, 1);
        vert(&c, x, y, z, u1, v1, edge_sh, 1, 1, 1);
        vert(&d, x, y + 1.0f, z, u1, v0, edge_sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* South edge */
        vert(&a, x, y + 1.0f, z + 1.0f, u0, v0, edge_sh, 1, 1, 1);
        vert(&b, x, y, z + 1.0f, u0, v1, edge_sh, 1, 1, 1);
        vert(&c, x + t, y, z + 1.0f, u1, v1, edge_sh, 1, 1, 1);
        vert(&d, x + t, y + 1.0f, z + 1.0f, u1, v0, edge_sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);

        /* Top edge */
        vert(&a, x, y + 1.0f, z, u0, v0, top_sh, 1, 1, 1);
        vert(&b, x, y + 1.0f, z + 1.0f, u0, v1, top_sh, 1, 1, 1);
        vert(&c, x + t, y + 1.0f, z + 1.0f, u1, v1, top_sh, 1, 1, 1);
        vert(&d, x + t, y + 1.0f, z, u1, v0, top_sh, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);
    }
}

static void emit_cactus(MeshBuffer *mb, const Assets *assets, uint8_t id,
                        float x, float y, float z, bool draw_top, bool draw_bottom, float light) {
    float u0, v0, u1, v1;
    RVertex a, b, c, d;

    /* Top face (full 1x1 at y + 1) */
    if (draw_top) {
        assets_tile_uv(assets, g_block_tiles[id][FACE_TOP], &u0, &v0, &u1, &v1);
        vert(&a, x,        y + 1.0f, z,        u0, v0, 1.0f * light, 1, 1, 1);
        vert(&b, x,        y + 1.0f, z + 1.0f, u0, v1, 1.0f * light, 1, 1, 1);
        vert(&c, x + 1.0f, y + 1.0f, z + 1.0f, u1, v1, 1.0f * light, 1, 1, 1);
        vert(&d, x + 1.0f, y + 1.0f, z,        u1, v0, 1.0f * light, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);
    }

    /* Bottom face (full 1x1 at y) */
    if (draw_bottom) {
        assets_tile_uv(assets, g_block_tiles[id][FACE_BOTTOM], &u0, &v0, &u1, &v1);
        vert(&a, x,        y, z + 1.0f, u0, v0, 0.5f * light, 1, 1, 1);
        vert(&b, x,        y, z,        u0, v1, 0.5f * light, 1, 1, 1);
        vert(&c, x + 1.0f, y, z,        u1, v1, 0.5f * light, 1, 1, 1);
        vert(&d, x + 1.0f, y, z + 1.0f, u1, v0, 0.5f * light, 1, 1, 1);
        mesh_buf_push(mb, a, b, c, d);
    }

    float sh_ns = 0.80f * light;
    float sh_ew = 0.65f * light;
    const float inset = 0.0625f; // 1/16th of a block inset matching vanilla cactus.json

    /* North face (at z + inset, facing -Z) */
    assets_tile_uv(assets, g_block_tiles[id][FACE_NORTH], &u0, &v0, &u1, &v1);
    vert(&a, x + 1.0f, y + 1.0f, z + inset, u0, v0, sh_ns, 1, 1, 1);
    vert(&b, x + 1.0f, y,        z + inset, u0, v1, sh_ns, 1, 1, 1);
    vert(&c, x,        y,        z + inset, u1, v1, sh_ns, 1, 1, 1);
    vert(&d, x,        y + 1.0f, z + inset, u1, v0, sh_ns, 1, 1, 1);
    mesh_buf_push(mb, a, b, c, d);

    /* South face (at z + 1 - inset, facing +Z) */
    assets_tile_uv(assets, g_block_tiles[id][FACE_SOUTH], &u0, &v0, &u1, &v1);
    vert(&a, x,        y + 1.0f, z + 1.0f - inset, u0, v0, sh_ns, 1, 1, 1);
    vert(&b, x,        y,        z + 1.0f - inset, u0, v1, sh_ns, 1, 1, 1);
    vert(&c, x + 1.0f, y,        z + 1.0f - inset, u1, v1, sh_ns, 1, 1, 1);
    vert(&d, x + 1.0f, y + 1.0f, z + 1.0f - inset, u1, v0, sh_ns, 1, 1, 1);
    mesh_buf_push(mb, a, b, c, d);

    /* West face (at x + inset, facing -X) */
    assets_tile_uv(assets, g_block_tiles[id][FACE_WEST], &u0, &v0, &u1, &v1);
    vert(&a, x + inset, y + 1.0f, z,        u0, v0, sh_ew, 1, 1, 1);
    vert(&b, x + inset, y,        z,        u0, v1, sh_ew, 1, 1, 1);
    vert(&c, x + inset, y,        z + 1.0f, u1, v1, sh_ew, 1, 1, 1);
    vert(&d, x + inset, y + 1.0f, z + 1.0f, u1, v0, sh_ew, 1, 1, 1);
    mesh_buf_push(mb, a, b, c, d);

    /* East face (at x + 1 - inset, facing +X) */
    assets_tile_uv(assets, g_block_tiles[id][FACE_EAST], &u0, &v0, &u1, &v1);
    vert(&a, x + 1.0f - inset, y + 1.0f, z + 1.0f, u0, v0, sh_ew, 1, 1, 1);
    vert(&b, x + 1.0f - inset, y,        z + 1.0f, u0, v1, sh_ew, 1, 1, 1);
    vert(&c, x + 1.0f - inset, y,        z,        u1, v1, sh_ew, 1, 1, 1);
    vert(&d, x + 1.0f - inset, y + 1.0f, z,        u1, v0, sh_ew, 1, 1, 1);
    mesh_buf_push(mb, a, b, c, d);
}

static inline bool is_leaves(uint8_t id) {
    return id == BLOCK_OAK_LEAVES || id == BLOCK_BIRCH_LEAVES ||
           id == BLOCK_SPRUCE_LEAVES || id == BLOCK_JUNGLE_LEAVES ||
           id == BLOCK_ACACIA_LEAVES || id == BLOCK_DARK_OAK_LEAVES ||
           id == BLOCK_CHERRY_LEAVES || id == BLOCK_PALE_OAK_LEAVES ||
           id == BLOCK_AZALEA_LEAVES || id == BLOCK_FLOWERING_AZALEA_LEAVES;
}

static inline bool is_submerged_flora(uint8_t id) {
    return id == BLOCK_SEAGRASS || id == BLOCK_KELP ||
           id == BLOCK_TUBE_CORAL || id == BLOCK_BRAIN_CORAL ||
           id == BLOCK_BUBBLE_CORAL || id == BLOCK_FIRE_CORAL ||
           id == BLOCK_HORN_CORAL;
}

static bool face_visible(uint8_t id, uint8_t nb) {
    if (block_is_opaque(nb))
        return false;
    if (id == BLOCK_WATER && is_submerged_flora(nb))
        return false;
    if (nb == id) {
        if (g_block_defs[id].shape == SHAPE_SLAB)
            return true;
        if (is_leaves(id))
            return true;
        return false;
    }
    return true;
}

typedef struct {
    Chunk *c;
    Chunk *nb_e, *nb_w, *nb_s, *nb_n;
    Chunk *nb_ne, *nb_nw, *nb_se, *nb_sw;
    const Assets *assets;
    bool smooth_lighting;
    uint32_t version;
    Renderer *r;
} MeshTask;

static inline bool is_chunk_ready(Chunk *c) {
    return c && c->generated && !c->generating;
}

static inline uint8_t fast_neighbour8(const MeshTask *task, int lx, int y, int lz, int dx, int dy, int dz) {
    int ny = y + dy;
    if (ny < 0 || ny >= WORLD_HEIGHT)
        return BLOCK_AIR;
    int nx = lx + dx;
    int nz = lz + dz;
    if (nx >= 0 && nx < CHUNK_SIZE && nz >= 0 && nz < CHUNK_SIZE)
        return task->c->blocks[chunk_index_local(nx, ny, nz)];
    if (nx >= CHUNK_SIZE && nz >= 0 && nz < CHUNK_SIZE)
        return is_chunk_ready(task->nb_e) ? task->nb_e->blocks[chunk_index_local(0, ny, nz)] : BLOCK_AIR;
    if (nx < 0 && nz >= 0 && nz < CHUNK_SIZE)
        return is_chunk_ready(task->nb_w) ? task->nb_w->blocks[chunk_index_local(CHUNK_SIZE - 1, ny, nz)] : BLOCK_AIR;
    if (nz >= CHUNK_SIZE && nx >= 0 && nx < CHUNK_SIZE)
        return is_chunk_ready(task->nb_s) ? task->nb_s->blocks[chunk_index_local(nx, ny, 0)] : BLOCK_AIR;
    if (nz < 0 && nx >= 0 && nx < CHUNK_SIZE)
        return is_chunk_ready(task->nb_n) ? task->nb_n->blocks[chunk_index_local(nx, ny, CHUNK_SIZE - 1)] : BLOCK_AIR;
    if (nx >= CHUNK_SIZE && nz < 0)
        return is_chunk_ready(task->nb_ne) ? task->nb_ne->blocks[chunk_index_local(0, ny, CHUNK_SIZE - 1)] : BLOCK_AIR;
    if (nx < 0 && nz < 0)
        return is_chunk_ready(task->nb_nw) ? task->nb_nw->blocks[chunk_index_local(CHUNK_SIZE - 1, ny, CHUNK_SIZE - 1)] : BLOCK_AIR;
    if (nx >= CHUNK_SIZE && nz >= CHUNK_SIZE)
        return is_chunk_ready(task->nb_se) ? task->nb_se->blocks[chunk_index_local(0, ny, 0)] : BLOCK_AIR;
    if (nx < 0 && nz >= CHUNK_SIZE)
        return is_chunk_ready(task->nb_sw) ? task->nb_sw->blocks[chunk_index_local(CHUNK_SIZE - 1, ny, 0)] : BLOCK_AIR;
    return BLOCK_AIR;
}

static inline bool is_solid_block8(const MeshTask *task, int lx, int y, int lz, int dx, int dy, int dz) {
    uint8_t b = fast_neighbour8(task, lx, y, lz, dx, dy, dz);
    return block_is_opaque(b);
}

typedef struct {
    int8_t lx, lz;
    int16_t y;
    uint8_t light;
} ChunkLightEmitter;

static inline float compute_light_level(const Chunk *c, const ChunkLightEmitter *emitters, int emitter_count,
                                        int lx, int y, int lz, float user_brightness) {
    /* 1. Sky light: 1.0 at or above surface heightmap, drops rapidly in enclosed caves */
    float sky_light = 1.0f;
    int hlx = lx < 0 ? 0 : (lx >= CHUNK_SIZE ? CHUNK_SIZE - 1 : lx);
    int hlz = lz < 0 ? 0 : (lz >= CHUNK_SIZE ? CHUNK_SIZE - 1 : lz);
    int surface_y = c->heightmap[hlx + hlz * CHUNK_SIZE];
    if (y < surface_y) {
        int depth = surface_y - y;
        if (depth <= 1) sky_light = 0.70f;
        else if (depth == 2) sky_light = 0.35f;
        else sky_light = 0.0f; /* Pitch-black underground cave */
    }

    /* 2. Block light from torches, lava, glowing blocks */
    float block_light = 0.0f;
    for (int i = 0; i < emitter_count; i++) {
        int d = abs((int)emitters[i].lx - lx) +
                abs((int)emitters[i].y - y) +
                abs((int)emitters[i].lz - lz);
        if (d < (int)emitters[i].light) {
            float bl = (float)(emitters[i].light - d) / 15.0f;
            if (bl > block_light) {
                block_light = bl;
                if (block_light >= 0.95f) break;
            }
        }
    }

    float min_amb = 0.08f + 0.08f * user_brightness;
    float l = fmaxf(sky_light, block_light);
    if (l < min_amb) l = min_amb;
    return l;
}

static void mesh_chunk_geometry(const MeshTask *task, MeshBuffer *mb, int *out_min_y, int *out_max_y) {
    static const float shade[FACE_COUNT] = {1.00f, 0.50f, 0.80f, 0.80f, 0.65f, 0.65f};
    static const int fdx[FACE_COUNT] = {0, 0, 0, 0, 1, -1};
    static const int fdy[FACE_COUNT] = {1, -1, 0, 0, 0, 0};
    static const int fdz[FACE_COUNT] = {0, 0, -1, 1, 0, 0};

    Chunk *c = task->c;
    int min_y = WORLD_HEIGHT;
    int max_y = 0;
    const Assets *assets = task->assets;

    /* Heightmap accelerated scan: 256 checks instead of 65536 checks */
    for (int i = 0; i < CHUNK_AREA; i++) {
        if (c->heightmap[i] > max_y)
            max_y = c->heightmap[i];
    }
    if (max_y == 0) {
        bool has_blocks = false;
        for (int i = 0; i < CHUNK_AREA; i++) {
            if (c->blocks[i] != BLOCK_AIR) {
                has_blocks = true;
                break;
            }
        }
        if (!has_blocks) {
            *out_min_y = 0;
            *out_max_y = 0;
            return;
        }
    }
    max_y += 16;
    if (max_y >= WORLD_HEIGHT) max_y = WORLD_HEIGHT - 1;

    ChunkLightEmitter emitters[128];
    int emitter_count = 0;
    for (int elx = 0; elx < CHUNK_SIZE; elx++) {
        for (int elz = 0; elz < CHUNK_SIZE; elz++) {
            for (int ey = 0; ey <= max_y; ey++) {
                uint8_t bid = c->blocks[chunk_index_local(elx, ey, elz)];
                if (bid < BLOCK_COUNT && g_block_defs[bid].light > 0) {
                    if (emitter_count < 128) {
                        emitters[emitter_count++] = (ChunkLightEmitter){
                            (int8_t)elx, (int8_t)elz, (int16_t)ey, g_block_defs[bid].light
                        };
                    }
                }
            }
        }
    }
    float user_br = task->r ? task->r->brightness : 0.5f;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            for (int y = 0; y <= max_y; y++) {
                uint8_t id = c->blocks[chunk_index_local(lx, y, lz)];
                if (id == BLOCK_AIR || id >= BLOCK_COUNT)
                    continue;
                const BlockDef *def = &g_block_defs[id];
                float block_l = compute_light_level(c, emitters, emitter_count, lx, y, lz, user_br);
                if (def->light > 0) block_l = 1.0f;

                if (def->shape == SHAPE_TORCH) {
                    if (y < min_y) min_y = y;
                    if (y + 1 > max_y) max_y = y + 1;
                    emit_cross(mb, assets, g_block_tiles[id][FACE_TOP],
                               (float)(c->cx * CHUNK_SIZE + lx), (float)y,
                               (float)(c->cz * CHUNK_SIZE + lz), 0.75f, 1.0f, 1.0f, 1.0f, 1.0f);
                    continue;
                }
                if (def->shape == SHAPE_CROSS) {
                    if (y < min_y) min_y = y;
                    if (y + 1 > max_y) max_y = y + 1;
                    float cr = (def->tint_mode != TINT_NONE) ? def->tint_r : 1.0f;
                    float cg = (def->tint_mode != TINT_NONE) ? def->tint_g : 1.0f;
                    float cb = (def->tint_mode != TINT_NONE) ? def->tint_b : 1.0f;
                    emit_cross(mb, assets, g_block_tiles[id][FACE_TOP],
                               (float)(c->cx * CHUNK_SIZE + lx), (float)y,
                               (float)(c->cz * CHUNK_SIZE + lz), 1.0f, block_l, cr, cg, cb);
                    continue;
                }
                if (def->shape == SHAPE_FLAT_FLOOR) {
                    if (y < min_y) min_y = y;
                    if (y + 1 > max_y) max_y = y + 1;
                    float r_val = def->tint_r, g_val = def->tint_g, b_val = def->tint_b;
                    uint8_t p = c->redstone_power[chunk_index_local(lx, y, lz)];
                    if (p > 0) {
                        r_val = 0.5f + (float)p / 30.0f;
                        g_val = 0.05f;
                        b_val = 0.05f;
                    }
                    emit_flat_floor(mb, assets, g_block_tiles[id][FACE_TOP],
                                    (float)(c->cx * CHUNK_SIZE + lx), (float)y,
                                    (float)(c->cz * CHUNK_SIZE + lz), block_l, r_val, g_val, b_val);
                    continue;
                }
                if (def->shape == SHAPE_DOOR) {
                    if (y < min_y) min_y = y;
                    if (y + 1 > max_y) max_y = y + 1;
                    bool is_top = (y > 0 && g_block_defs[c->blocks[chunk_index_local(lx, y - 1, lz)]].shape == SHAPE_DOOR);
                    emit_door(mb, assets, id, is_top,
                              (float)(c->cx * CHUNK_SIZE + lx), (float)y,
                              (float)(c->cz * CHUNK_SIZE + lz), block_l);
                    continue;
                }
                if (def->shape == SHAPE_CACTUS) {
                    if (y < min_y) min_y = y;
                    if (y + 1 > max_y) max_y = y + 1;
                    uint8_t nb_up = fast_neighbour8(task, lx, y, lz, 0, 1, 0);
                    uint8_t nb_dn = fast_neighbour8(task, lx, y, lz, 0, -1, 0);
                    emit_cactus(mb, assets, id,
                                (float)(c->cx * CHUNK_SIZE + lx), (float)y,
                                (float)(c->cz * CHUNK_SIZE + lz),
                                nb_up != BLOCK_CACTUS, nb_dn != BLOCK_CACTUS, block_l);
                    continue;
                }
                float h = def->liquid ? 0.875f : (def->shape == SHAPE_SLAB ? 0.5f : 1.0f);
                for (int f = 0; f < FACE_COUNT; f++) {
                    uint8_t nb = fast_neighbour8(task, lx, y, lz, fdx[f], fdy[f], fdz[f]);
                    if (!face_visible(id, nb))
                        continue;
                    float tt = 1, tg2 = 1, tb2 = 1;
                    if (def->tint_mode == TINT_ALL ||
                        (def->tint_mode == TINT_TOP && f == FACE_TOP)) {
                        tt = def->tint_r;
                        tg2 = def->tint_g;
                        tb2 = def->tint_b;
                    }
                    int nx = lx + fdx[f], ny = y + fdy[f], nz = lz + fdz[f];
                    float face_l = compute_light_level(c, emitters, emitter_count, nx, ny, nz, user_br);
                    if (def->light > 0) face_l = 1.0f;
                    float sh = shade[f] * face_l;
                    if (def->light > 0)
                        sh = 1.0f;
                    if (y < min_y)
                        min_y = y;
                    if (y + 1 > max_y)
                        max_y = y + 1;

                    float ao[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    const float *ao_ptr = NULL;
                    if (task->smooth_lighting && def->light == 0 && !def->liquid) {
                        #define S(dx, dy, dz) is_solid_block8(task, lx, y, lz, dx, dy, dz)
                        switch (f) {
                        case FACE_TOP:
                            ao[0] = vertex_ao(S(-1, 1, 0), S(0, 1, -1), S(-1, 1, -1));
                            ao[1] = vertex_ao(S(-1, 1, 0), S(0, 1, 1),  S(-1, 1, 1));
                            ao[2] = vertex_ao(S(1, 1, 0),  S(0, 1, 1),  S(1, 1, 1));
                            ao[3] = vertex_ao(S(1, 1, 0),  S(0, 1, -1), S(1, 1, -1));
                            break;
                        case FACE_BOTTOM:
                            ao[0] = vertex_ao(S(-1, -1, 0), S(0, -1, -1), S(-1, -1, -1));
                            ao[1] = vertex_ao(S(1, -1, 0),  S(0, -1, -1), S(1, -1, -1));
                            ao[2] = vertex_ao(S(1, -1, 0),  S(0, -1, 1),  S(1, -1, 1));
                            ao[3] = vertex_ao(S(-1, -1, 0), S(0, -1, 1),  S(-1, -1, 1));
                            break;
                        case FACE_NORTH:
                            ao[0] = vertex_ao(S(1, 0, -1), S(0, 1, -1), S(1, 1, -1));
                            ao[1] = vertex_ao(S(1, 0, -1), S(0, -1, -1), S(1, -1, -1));
                            ao[2] = vertex_ao(S(-1, 0, -1), S(0, -1, -1), S(-1, -1, -1));
                            ao[3] = vertex_ao(S(-1, 0, -1), S(0, 1, -1), S(-1, 1, -1));
                            break;
                        case FACE_SOUTH:
                            ao[0] = vertex_ao(S(-1, 0, 1), S(0, 1, 1), S(-1, 1, 1));
                            ao[1] = vertex_ao(S(-1, 0, 1), S(0, -1, 1), S(-1, -1, 1));
                            ao[2] = vertex_ao(S(1, 0, 1),  S(0, -1, 1), S(1, -1, 1));
                            ao[3] = vertex_ao(S(1, 0, 1),  S(0, 1, 1),  S(1, 1, 1));
                            break;
                        case FACE_EAST:
                            ao[0] = vertex_ao(S(1, 0, 1),  S(1, 1, 0), S(1, 1, 1));
                            ao[1] = vertex_ao(S(1, 0, 1),  S(1, -1, 0), S(1, -1, 1));
                            ao[2] = vertex_ao(S(1, 0, -1), S(1, -1, 0), S(1, -1, -1));
                            ao[3] = vertex_ao(S(1, 0, -1), S(1, 1, 0), S(1, 1, -1));
                            break;
                        case FACE_WEST:
                            ao[0] = vertex_ao(S(-1, 0, -1), S(-1, 1, 0), S(-1, 1, -1));
                            ao[1] = vertex_ao(S(-1, 0, -1), S(-1, -1, 0), S(-1, -1, -1));
                            ao[2] = vertex_ao(S(-1, 0, 1),  S(-1, -1, 0), S(-1, -1, 1));
                            ao[3] = vertex_ao(S(-1, 0, 1),  S(-1, 1, 0), S(-1, 1, 1));
                            break;
                        }
                        #undef S
                        ao_ptr = ao;
                    }

                    emit_face(mb, assets, f, g_block_tiles[id][f],
                              (float)(c->cx * CHUNK_SIZE + lx),
                              (float)y,
                              (float)(c->cz * CHUNK_SIZE + lz),
                              h, sh, tt, tg2, tb2, ao_ptr);
                }
            }
        }
    }
    *out_min_y = min_y;
    *out_max_y = max_y;
}

static void chunk_mesh_worker(void *arg) {
    MeshTask *task = (MeshTask *)arg;
    MeshBuffer mb = {0};
    mb.capacity = 16384;
    mb.verts = malloc((size_t)mb.capacity * sizeof(RVertex));

    int min_y = WORLD_HEIGHT;
    int max_y = 0;
    mesh_chunk_geometry(task, &mb, &min_y, &max_y);

    Renderer *r = task->r;
    CompletedMesh cm;
    cm.cx = task->c->cx;
    cm.cz = task->c->cz;
    cm.version = task->version;
    cm.verts = mb.verts;
    cm.count = mb.count;
    cm.min_y = min_y;
    cm.max_y = max_y;

    if (r->completed_mutex) {
        pthread_mutex_lock((pthread_mutex_t *)r->completed_mutex);
        if (r->completed_count == r->completed_capacity) {
            r->completed_capacity = r->completed_capacity ? r->completed_capacity * 2 : 64;
            r->completed_meshes = xrealloc(r->completed_meshes, (size_t)r->completed_capacity * sizeof(CompletedMesh));
        }
        r->completed_meshes[r->completed_count++] = cm;
        pthread_mutex_unlock((pthread_mutex_t *)r->completed_mutex);
    } else {
        free(mb.verts);
    }

    free(task);
}

static int chunk_distance(int cx, int cz, int pcx, int pcz) {
    int dx = cx - pcx, dz = cz - pcz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx > dz ? dx : dz;
}

static void sync_chunks(Renderer *r, World *w, Vec3 eye, int budget, bool all) {
    int pcx = (int)floorf(eye.x / CHUNK_SIZE);
    int pcz = (int)floorf(eye.z / CHUNK_SIZE);
    r->last_rebuilds = 0;

    if (pcx != r->last_pcx || pcz != r->last_pcz || w->dirty) {
        r->last_pcx = pcx;
        r->last_pcz = pcz;
        r->all_built = false;
    }

    /* 1. Drain completed meshes from background worker threads */
    int drain_limit = all ? 999999 : 16;
    int drained = 0;
    while (drained < drain_limit) {
        CompletedMesh cm;
        if (!r->completed_mutex) break;
        pthread_mutex_lock((pthread_mutex_t *)r->completed_mutex);
        if (r->completed_count == 0) {
            pthread_mutex_unlock((pthread_mutex_t *)r->completed_mutex);
            break;
        }
        cm = r->completed_meshes[--r->completed_count];
        pthread_mutex_unlock((pthread_mutex_t *)r->completed_mutex);

        ChunkMesh *m = mesh_find(r, cm.cx, cm.cz);
        if (m && m->version == cm.version) {
            m->min_y = cm.min_y;
            m->max_y = cm.max_y;
            m->count = cm.count;
#if MCED_USE_GL
            if (cm.count > 0) {
                if (!m->list)
                    m->list = glGenLists(1);
                glNewList(m->list, GL_COMPILE);
                glEnableClientState(GL_VERTEX_ARRAY);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glEnableClientState(GL_COLOR_ARRAY);
                glVertexPointer(3, GL_FLOAT, sizeof(RVertex), &cm.verts[0].x);
                glTexCoordPointer(2, GL_FLOAT, sizeof(RVertex), &cm.verts[0].u);
                glColorPointer(3, GL_FLOAT, sizeof(RVertex), &cm.verts[0].r);
                glDrawArrays(GL_QUADS, 0, cm.count);
                glEndList();
            } else if (m->list) {
                glDeleteLists(m->list, 1);
                m->list = 0;
            }
#else
            if (cm.count > 0) {
                if (m->capacity < cm.count) {
                    m->capacity = cm.count;
                    m->verts = xrealloc(m->verts, (size_t)m->capacity * sizeof(RVertex));
                }
                memcpy(m->verts, cm.verts, (size_t)cm.count * sizeof(RVertex));
            }
#endif
            m->built = true;
            m->meshing = false;
            r->last_rebuilds++;
            r->total_rebuilds++;
        } else if (m) {
            m->meshing = false;
        }
        free(cm.verts);
        drained++;
    }

    /* 2. Dispatch chunk meshing jobs */
    if (!r->all_built || all) {
        bool any_missing = false;
        int submit_limit = all ? 999999 : 16;
        int submitted = 0;
        for (int rad = 0; rad <= r->render_radius; rad++) {
            if (!all && submitted >= submit_limit) {
                any_missing = true;
                break;
            }
            for (int dz = -rad; dz <= rad; dz++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    if (rad > 0 && abs(dx) != rad && abs(dz) != rad)
                        continue;
                    int cx = pcx + dx;
                    int cz = pcz + dz;
                    Chunk *c = world_find_chunk(w, cx, cz, false);
                    if (!c || !c->generated) {
                        any_missing = true;
                        continue;
                    }
                    ChunkMesh *m = mesh_get(r, cx, cz);
                    if ((c->dirty || !m->built) && !m->meshing) {
                        m->meshing = true;
                        c->dirty = false;
                        m->version = c->version;

                        MeshTask *task = malloc(sizeof(MeshTask));
                        task->c = c;
                        task->nb_e  = world_find_chunk(w, cx + 1, cz, false);
                        task->nb_w  = world_find_chunk(w, cx - 1, cz, false);
                        task->nb_s  = world_find_chunk(w, cx, cz + 1, false);
                        task->nb_n  = world_find_chunk(w, cx, cz - 1, false);
                        task->nb_ne = world_find_chunk(w, cx + 1, cz - 1, false);
                        task->nb_nw = world_find_chunk(w, cx - 1, cz - 1, false);
                        task->nb_se = world_find_chunk(w, cx + 1, cz + 1, false);
                        task->nb_sw = world_find_chunk(w, cx - 1, cz + 1, false);
                        task->assets = r->assets;
                        task->smooth_lighting = r->smooth_lighting;
                        task->version = m->version;
                        task->r = r;

                        if (r->pool) {
                            thread_pool_submit(r->pool, chunk_mesh_worker, task);
                        } else {
                            chunk_mesh_worker(task);
                        }
                        submitted++;
                        if (!all && submitted >= submit_limit) {
                            any_missing = true;
                            goto mesh_dispatch_done;
                        }
                    } else if (m->meshing) {
                        any_missing = true;
                    }
                }
            }
        }
mesh_dispatch_done:;
        if (all && r->pool) {
            thread_pool_wait_idle(r->pool);
            while (1) {
                CompletedMesh cm;
                pthread_mutex_lock((pthread_mutex_t *)r->completed_mutex);
                if (r->completed_count == 0) {
                    pthread_mutex_unlock((pthread_mutex_t *)r->completed_mutex);
                    break;
                }
                cm = r->completed_meshes[--r->completed_count];
                pthread_mutex_unlock((pthread_mutex_t *)r->completed_mutex);

                ChunkMesh *m = mesh_find(r, cm.cx, cm.cz);
                if (m && m->version == cm.version) {
                    m->min_y = cm.min_y;
                    m->max_y = cm.max_y;
                    m->count = cm.count;
#if MCED_USE_GL
                    if (cm.count > 0) {
                        if (!m->list)
                            m->list = glGenLists(1);
                        glNewList(m->list, GL_COMPILE);
                        glEnableClientState(GL_VERTEX_ARRAY);
                        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                        glEnableClientState(GL_COLOR_ARRAY);
                        glVertexPointer(3, GL_FLOAT, sizeof(RVertex), &cm.verts[0].x);
                        glTexCoordPointer(2, GL_FLOAT, sizeof(RVertex), &cm.verts[0].u);
                        glColorPointer(3, GL_FLOAT, sizeof(RVertex), &cm.verts[0].r);
                        glDrawArrays(GL_QUADS, 0, cm.count);
                        glEndList();
                    } else if (m->list) {
                        glDeleteLists(m->list, 1);
                        m->list = 0;
                    }
#else
                    if (cm.count > 0) {
                        if (m->capacity < cm.count) {
                            m->capacity = cm.count;
                            m->verts = xrealloc(m->verts, (size_t)m->capacity * sizeof(RVertex));
                        }
                        memcpy(m->verts, cm.verts, (size_t)cm.count * sizeof(RVertex));
                    }
#endif
                    m->built = true;
                    m->meshing = false;
                    r->last_rebuilds++;
                    r->total_rebuilds++;
                } else if (m) {
                    m->meshing = false;
                }
                free(cm.verts);
            }
        }
        if (!any_missing && submitted == 0 && r->completed_count == 0) {
            r->all_built = true;
            w->dirty = false;
        }
    }

    /* 3. Unload distant meshes outside render radius + 2 hysteresis */
    bool pruned = false;
    for (int i = r->chunk_count - 1; i >= 0; i--) {
        ChunkMesh *m = &r->chunks[i];
        if (chunk_distance(m->cx, m->cz, pcx, pcz) > r->render_radius + 2) {
            free(m->verts);
            m->verts = NULL;
            m->built = false;
            m->meshing = false;
#if MCED_USE_GL
            if (m->list) {
                glDeleteLists(m->list, 1);
                m->list = 0;
            }
#endif
            r->chunks[i] = r->chunks[--r->chunk_count];
            pruned = true;
        }
    }
    if (pruned) {
        mesh_hash_rebuild(r);
    }
}

void renderer_warmup(Renderer *r, World *w, Vec3 eye) {
    sync_chunks(r, w, eye, 0, true);
}

void renderer_reset_chunks(Renderer *r) {
    if (r->pool) {
        thread_pool_wait_idle(r->pool);
    }
    if (r->completed_mutex) {
        pthread_mutex_lock((pthread_mutex_t *)r->completed_mutex);
        for (int i = 0; i < r->completed_count; i++) {
            free(r->completed_meshes[i].verts);
        }
        r->completed_count = 0;
        pthread_mutex_unlock((pthread_mutex_t *)r->completed_mutex);
    }
    for (int i = 0; i < r->chunk_count; i++) {
        free(r->chunks[i].verts);
        r->chunks[i].verts = NULL;
#if MCED_USE_GL
        if (r->chunks[i].list) {
            glDeleteLists(r->chunks[i].list, 1);
            r->chunks[i].list = 0;
        }
#endif
    }
    r->chunk_count = 0;
    r->last_pcx = 999999;
    r->last_pcz = 999999;
    r->all_built = false;
    if (r->mesh_hash && r->mesh_hash_capacity > 0)
        memset(r->mesh_hash, 0, (size_t)r->mesh_hash_capacity * sizeof(ChunkMesh *));
}

typedef struct {
    float nx, ny, nz, d;
} FrustumPlane;

static bool aabb_outside(const FrustumPlane *planes, float x0, float y0, float z0,
                         float x1, float y1, float z1) {
    for (int i = 0; i < 6; i++) {
        const FrustumPlane *p = &planes[i];
        float px = p->nx > 0 ? x1 : x0;
        float py = p->ny > 0 ? y1 : y0;
        float pz = p->nz > 0 ? z1 : z0;
        if (p->nx * px + p->ny * py + p->nz * pz + p->d < 0)
            return true;
    }
    return false;
}

/* Build the six view-frustum planes from TinyGL's actual projection and
   modelview matrices.  TinyGL returns column-major arrays, so the world
   to clip transform is C = P * M in the usual column-vector sense. */
static void build_frustum(FrustumPlane planes[6]) {
    GLfloat proj[16], model[16], m[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, model);
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++)
                sum += proj[k * 4 + row] * model[col * 4 + k];
            m[col * 4 + row] = sum;
        }
    }
    /* Row i of the combined matrix. */
    float rows[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            rows[i][j] = m[j * 4 + i];
    static const int idx[6] = {0, 0, 1, 1, 2, 2};
    static const float sgn[6] = {1, -1, 1, -1, 1, -1};
    for (int i = 0; i < 6; i++) {
        float a = rows[3][0] + sgn[i] * rows[idx[i]][0];
        float b = rows[3][1] + sgn[i] * rows[idx[i]][1];
        float c = rows[3][2] + sgn[i] * rows[idx[i]][2];
        float d = rows[3][3] + sgn[i] * rows[idx[i]][3];
        float len = sqrtf(a * a + b * b + c * c);
        if (len < 1e-6f) len = 1;
        planes[i].nx = a / len;
        planes[i].ny = b / len;
        planes[i].nz = c / len;
        planes[i].d = d / len;
    }
}

void renderer_world(Renderer *r, World *w, Vec3 eye, float yaw, float pitch, int rebuild_budget) {
    sync_chunks(r, w, eye, rebuild_budget, false);

    int pcx = (int)floorf(eye.x / CHUNK_SIZE);
    int pcz = (int)floorf(eye.z / CHUNK_SIZE);

    set_perspective(r, eye, yaw, pitch);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBindTexture(GL_TEXTURE_2D, r->atlas_tex);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

#if MCED_USE_GL
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
    /* Fog handling for underwater, under-lava, and atmosphere */
    if (r->submerged_fluid == 1) {
        /* Dense murky underwater fog */
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_EXP2);
        glFogf(GL_FOG_DENSITY, 0.08f);
        glFogfv(GL_FOG_COLOR, r->fog_color);
    } else if (r->submerged_fluid == 2) {
        /* Extreme thick fiery lava fog (visible only ~2 blocks) */
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, r->fog_color);
        glFogf(GL_FOG_START, 0.1f);
        glFogf(GL_FOG_END, 2.4f);
    } else if (r->fog_enabled) {
        float fog_dist = (float)(r->render_radius * CHUNK_SIZE);
        float fog_start = fog_dist * 0.70f;
        float fog_end = fog_dist * 0.98f;
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, r->fog_color);
        glFogf(GL_FOG_START, fog_start);
        glFogf(GL_FOG_END, fog_end);
    } else {
        glDisable(GL_FOG);
    }

    /* Ambient scene lighting based on day/night cycle and submerged state */
    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    float day_cycle = fmodf(r->world_time, 1200.0f);
    if (day_cycle < 0.0f) day_cycle += 1200.0f;
    float c_angle = ((day_cycle - 300.0f) / 1200.0f) * 2.0f * (float)M_PI;
    float s_elev = cosf(c_angle);

    GLfloat amb[4];
    if (r->submerged_fluid == 1) {
        /* Aquatic tint */
        amb[0] = 0.35f; amb[1] = 0.55f; amb[2] = 0.80f; amb[3] = 1.0f;
    } else if (r->submerged_fluid == 2) {
        /* Lava intense fiery glow */
        amb[0] = 1.00f; amb[1] = 0.40f; amb[2] = 0.10f; amb[3] = 1.0f;
    } else if (s_elev > 0.20f) {
        /* Daytime full brightness */
        amb[0] = 1.0f; amb[1] = 1.0f; amb[2] = 1.0f; amb[3] = 1.0f;
    } else if (s_elev > -0.20f) {
        /* Sunrise / Sunset */
        float t = (s_elev + 0.20f) / 0.40f;
        float sunset_w = 1.0f - fabsf(t - 0.5f) * 2.0f;
        if (sunset_w < 0.0f) sunset_w = 0.0f;
        amb[0] = 0.25f + 0.75f * t + 0.20f * sunset_w;
        amb[1] = 0.28f + 0.72f * t + 0.05f * sunset_w;
        amb[2] = 0.38f + 0.62f * t;
        amb[3] = 1.0f;
    } else {
        /* Nighttime dark moonlit atmosphere */
        amb[0] = 0.25f; amb[1] = 0.28f; amb[2] = 0.40f; amb[3] = 1.0f;
    }
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
#endif

    r->last_quads = 0;
    r->last_chunks = 0;
    FrustumPlane planes[6];
    build_frustum(planes);

    for (int i = 0; i < r->chunk_count; i++) {
        ChunkMesh *m = &r->chunks[i];
        if (m->count == 0)
            continue;
        int d = chunk_distance(m->cx, m->cz, pcx, pcz);
        if (d > r->render_radius)
            continue;
        float cx = (float)(m->cx * CHUNK_SIZE + 8);
        float cz = (float)(m->cz * CHUNK_SIZE + 8);
        float dx = cx - eye.x, dz = cz - eye.z;
        if (dx * dx + dz * dz > (float)((r->render_radius + 1) * CHUNK_SIZE) *
                                      (float)((r->render_radius + 1) * CHUNK_SIZE))
            continue;
        float y0 = (float)m->min_y, y1 = (float)m->max_y;
        if (aabb_outside(planes, (float)(m->cx * CHUNK_SIZE), y0,
                         (float)(m->cz * CHUNK_SIZE),
                         (float)(m->cx * CHUNK_SIZE + CHUNK_SIZE), y1,
                         (float)(m->cz * CHUNK_SIZE + CHUNK_SIZE)))
            continue;

#if MCED_USE_GL
        if (m->list) {
            glCallList(m->list);
        } else if (m->verts) {
            glVertexPointer(3, GL_FLOAT, sizeof(RVertex), &m->verts[0].x);
            glTexCoordPointer(2, GL_FLOAT, sizeof(RVertex), &m->verts[0].u);
            glColorPointer(3, GL_FLOAT, sizeof(RVertex), &m->verts[0].r);
            glDrawArrays(GL_QUADS, 0, m->count);
        }
#else
        glVertexPointer(3, GL_FLOAT, 5, &m->verts[0].x);
        glTexCoordPointer(2, GL_FLOAT, 6, &m->verts[0].u);
        glColorPointer(3, GL_FLOAT, 5, &m->verts[0].r);
        glDrawArrays(GL_QUADS, 0, m->count);
#endif
        r->last_quads += m->count / 4;
        r->last_chunks++;
    }

#if MCED_USE_GL
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
#endif

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_CULL_FACE);
}

void renderer_outline(Renderer *r, int bx, int by, int bz, bool visible) {
    if (!visible)
        return;
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_COLOR_ARRAY);
    glEnable(GL_DEPTH_TEST);
    const float e = 0.003f;
    float x0 = bx - e, y0 = by - e, z0 = bz - e;
    float x1 = bx + 1 + e, y1 = by + 1 + e, z1 = bz + 1 + e;
    glColor3f(0.03f, 0.03f, 0.03f);
    glBegin(GL_LINES);
    /* bottom */
    glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
    glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0);
    /* top */
    glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
    glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
    /* posts */
    glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
    glEnd();
}

void renderer_draw_break_progress(Renderer *r, int bx, int by, int bz, float progress) {
    if (progress <= 0.0f)
        return;
    int stage = (int)(progress * 10.0f);
    if (stage < 0) stage = 0;
    if (stage > 9) stage = 9;
    uint16_t tile = r->break_stage_tiles[stage];

    float u0, v0, u1, v1;
    assets_tile_uv(r->assets, tile, &u0, &v0, &u1, &v1);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->atlas_tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    glDepthMask(0);
#if MCED_USE_GL
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
#endif

    glColor3f(1.0f, 1.0f, 1.0f);
    float x = (float)bx, y = (float)by, z = (float)bz;
    const float e = 0.002f;

    glBegin(GL_QUADS);
    /* TOP */
    glTexCoord2f(u0, v0); glVertex3f(x - e, y + 1.0f + e, z - e);
    glTexCoord2f(u0, v1); glVertex3f(x - e, y + 1.0f + e, z + 1.0f + e);
    glTexCoord2f(u1, v1); glVertex3f(x + 1.0f + e, y + 1.0f + e, z + 1.0f + e);
    glTexCoord2f(u1, v0); glVertex3f(x + 1.0f + e, y + 1.0f + e, z - e);
    /* BOTTOM */
    glTexCoord2f(u0, v0); glVertex3f(x - e, y - e, z + 1.0f + e);
    glTexCoord2f(u0, v1); glVertex3f(x - e, y - e, z - e);
    glTexCoord2f(u1, v1); glVertex3f(x + 1.0f + e, y - e, z - e);
    glTexCoord2f(u1, v0); glVertex3f(x + 1.0f + e, y - e, z + 1.0f + e);
    /* NORTH */
    glTexCoord2f(u0, v0); glVertex3f(x + 1.0f + e, y + 1.0f + e, z - e);
    glTexCoord2f(u0, v1); glVertex3f(x + 1.0f + e, y - e, z - e);
    glTexCoord2f(u1, v1); glVertex3f(x - e, y - e, z - e);
    glTexCoord2f(u1, v0); glVertex3f(x - e, y + 1.0f + e, z - e);
    /* SOUTH */
    glTexCoord2f(u0, v0); glVertex3f(x - e, y + 1.0f + e, z + 1.0f + e);
    glTexCoord2f(u0, v1); glVertex3f(x - e, y - e, z + 1.0f + e);
    glTexCoord2f(u1, v1); glVertex3f(x + 1.0f + e, y - e, z + 1.0f + e);
    glTexCoord2f(u1, v0); glVertex3f(x + 1.0f + e, y + 1.0f + e, z + 1.0f + e);
    /* WEST */
    glTexCoord2f(u0, v0); glVertex3f(x - e, y + 1.0f + e, z - e);
    glTexCoord2f(u0, v1); glVertex3f(x - e, y - e, z - e);
    glTexCoord2f(u1, v1); glVertex3f(x - e, y - e, z + 1.0f + e);
    glTexCoord2f(u1, v0); glVertex3f(x - e, y + 1.0f + e, z + 1.0f + e);
    /* EAST */
    glTexCoord2f(u0, v0); glVertex3f(x + 1.0f + e, y + 1.0f + e, z + 1.0f + e);
    glTexCoord2f(u0, v1); glVertex3f(x + 1.0f + e, y - e, z + 1.0f + e);
    glTexCoord2f(u1, v1); glVertex3f(x + 1.0f + e, y - e, z - e);
    glTexCoord2f(u1, v0); glVertex3f(x + 1.0f + e, y + 1.0f + e, z - e);
    glEnd();

#if MCED_USE_GL
    glDisable(GL_POLYGON_OFFSET_FILL);
#endif
    glDepthMask(1);
    glDisable(GL_BLEND);
}

/* ------------------------------------------------------------------ */
/* Block break/place particles                                         */
/* ------------------------------------------------------------------ */

static uint32_t particle_rng = 0x9E3779B9u;

static float prand(void) {
    particle_rng ^= particle_rng << 13;
    particle_rng ^= particle_rng >> 17;
    particle_rng ^= particle_rng << 5;
    return (float)(particle_rng >> 8) * (1.0f / 16777216.0f);
}

static RenderParticle *particle_alloc(Renderer *r) {
    if (r->particle_count >= RENDER_MAX_PARTICLES)
        return NULL;
    RenderParticle *p = &r->particles[r->particle_count++];
    memset(p, 0, sizeof(*p));
    return p;
}

/* Pick a random pixel from one face of the block to colour the fragment.
   Flat-coloured chips avoid texture filtering artefacts at the tiny
   on-screen size of a particle, and they still look like the block. */
static void particle_pick_tile(Renderer *r, RenderParticle *p, uint8_t block) {
    int face = (int)(prand() * (float)FACE_COUNT);
    if (face >= FACE_COUNT)
        face = FACE_SOUTH;
    int tile = g_block_tiles[block][face];
    int tx = (tile % MCED_ATLAS_TILES) * MCED_ATLAS_TILE;
    int ty = (tile / MCED_ATLAS_TILES) * MCED_ATLAS_TILE;
    int ox = (int)(prand() * 16.0f);
    int oy = (int)(prand() * 16.0f);
    if (ox > 15) ox = 15;
    if (oy > 15) oy = 15;
    const uint8_t *px = r->assets->atlas_rgb +
        ((size_t)(ty + oy) * MCED_ATLAS_SIZE + (tx + ox)) * 3;
    float shade = p->r;
    p->r = fminf(1.0f, (float)px[0] / 255.0f * shade);
    p->g = fminf(1.0f, (float)px[1] / 255.0f * shade);
    p->b = fminf(1.0f, (float)px[2] / 255.0f * shade);
}

static void particle_common(RenderParticle *p, float life, float size) {
    p->max_life = life;
    p->life = life;
    p->size = size;
    p->rot = prand() * 6.2831853f;
    p->rot_speed = (prand() - 0.5f) * 9.0f;
    float b = 0.82f + prand() * 0.30f;
    p->r = b * (0.92f + prand() * 0.10f);
    p->g = b * (0.92f + prand() * 0.10f);
    p->b = b * (0.92f + prand() * 0.10f);
}

void renderer_particles_clear(Renderer *r) {
    r->particle_count = 0;
}

void renderer_particles_break(Renderer *r, uint8_t block, int bx, int by, int bz) {
    if (block == BLOCK_AIR || block >= BLOCK_COUNT)
        return;
    float cx = (float)bx + 0.5f;
    float cy = (float)by + 0.5f;
    float cz = (float)bz + 0.5f;
    int count = 6 + (int)(prand() * 5.0f);
    for (int i = 0; i < count; i++) {
        RenderParticle *p = particle_alloc(r);
        if (!p)
            break;
        Vec3 off = vec3((prand() - 0.5f) * 0.72f,
                        (prand() - 0.5f) * 0.72f,
                        (prand() - 0.5f) * 0.72f);
        p->pos = vec3(cx + off.x, cy + off.y, cz + off.z);
        p->vel = vec3(off.x * 5.0f + (prand() - 0.5f) * 1.5f,
                      off.y * 3.0f + 1.2f + prand() * 2.6f,
                      off.z * 5.0f + (prand() - 0.5f) * 1.5f);
        particle_common(p, 0.45f + prand() * 0.65f, 0.075f + prand() * 0.085f);
        particle_pick_tile(r, p, block);
    }
}

void renderer_particles_place(Renderer *r, World *w, uint8_t block,
                              int bx, int by, int bz, int nx, int ny, int nz) {
    if (block == BLOCK_AIR || block >= BLOCK_COUNT)
        return;
    static const int dirs[6][3] = {
        {0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}
    };
    float cx = (float)bx + 0.5f;
    float cy = (float)by + 0.5f;
    float cz = (float)bz + 0.5f;
    int count = 5 + (int)(prand() * 5.0f);
    for (int i = 0; i < count; i++) {
        RenderParticle *p = particle_alloc(r);
        if (!p)
            break;
        /* Prefer a face that touches open air, so fragments are visible. */
        int d = -1;
        for (int tries = 0; tries < 8; tries++) {
            int k = (int)(prand() * 6.0f) % 6;
            int ax = bx + dirs[k][0], ay = by + dirs[k][1], az = bz + dirs[k][2];
            uint8_t nb = world_get_block(w, ax, ay, az);
            if (!block_is_solid(nb)) {
                d = k;
                break;
            }
        }
        if (d < 0)
            d = 0;
        float dx = (float)dirs[d][0], dy = (float)dirs[d][1], dz = (float)dirs[d][2];
        p->pos = vec3(cx + dx * 0.56f + (prand() - 0.5f) * 0.34f * (1.0f - fabsf(dx)),
                      cy + dy * 0.56f + (prand() - 0.5f) * 0.34f * (1.0f - fabsf(dy)),
                      cz + dz * 0.56f + (prand() - 0.5f) * 0.34f * (1.0f - fabsf(dz)));
        p->vel = vec3(dx * (1.0f + prand() * 1.6f) + (prand() - 0.5f) * 1.2f,
                      dy * (1.0f + prand() * 1.2f) + 0.4f + prand() * 0.8f,
                      dz * (1.0f + prand() * 1.6f) + (prand() - 0.5f) * 1.2f);
        particle_common(p, 0.30f + prand() * 0.35f, 0.055f + prand() * 0.06f);
        particle_pick_tile(r, p, block);
    }
    (void)nx; (void)ny; (void)nz;
}

static bool particle_solid_at(World *w, float x, float y, float z) {
    int bx = (int)floorf(x);
    int by = (int)floorf(y);
    int bz = (int)floorf(z);
    if (by < 0)
        return true;
    if (by >= WORLD_HEIGHT)
        return false;
    return block_is_solid(world_get_block(w, bx, by, bz));
}

void renderer_particles_update(Renderer *r, World *w, float dt) {
    if (dt <= 0)
        return;
    for (int i = r->particle_count - 1; i >= 0; i--) {
        RenderParticle *p = &r->particles[i];
        p->life -= dt;
        if (p->life <= 0) {
            r->particles[i] = r->particles[--r->particle_count];
            continue;
        }
        p->rot += p->rot_speed * dt;
        p->vel.y -= 17.0f * dt;
        float drag = expf(-1.1f * dt);
        p->vel.x *= drag;
        p->vel.z *= drag;

        Vec3 old = p->pos;
        Vec3 np = vec3_add(old, vec3_mul(p->vel, dt));
        if (!particle_solid_at(w, old.x, old.y, old.z)) {
            if (particle_solid_at(w, np.x, old.y, old.z)) {
                np.x = old.x;
                p->vel.x *= -0.18f;
            }
            if (particle_solid_at(w, np.x, np.y, old.z)) {
                np.y = old.y;
                p->vel.y *= -0.16f;
                p->vel.x *= 0.55f;
                p->vel.z *= 0.55f;
            }
            if (particle_solid_at(w, np.x, np.y, np.z)) {
                np.z = old.z;
                p->vel.z *= -0.18f;
            }
        }
        p->pos = np;
    }
}

void renderer_particles_draw(Renderer *r, Vec3 eye, float yaw, float pitch) {
    if (r->particle_count == 0)
        return;
    float yr = yaw * MCED_DEG2RAD;
    float pr = pitch * MCED_DEG2RAD;
    Vec3 fwd = vec3(-sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr));
    Vec3 right = vec3(cosf(yr), 0.0f, -sinf(yr));
    Vec3 up = vec3_cross(right, fwd);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    for (int i = 0; i < r->particle_count; i++) {
        RenderParticle *p = &r->particles[i];
        float fade = p->life < 0.18f ? p->life / 0.18f : 1.0f;
        float s = p->size * (0.35f + 0.65f * fade);
        float ca = cosf(p->rot), sa = sinf(p->rot);
        Vec3 rx = vec3_add(vec3_mul(right, ca), vec3_mul(up, sa));
        Vec3 uy = vec3_add(vec3_mul(up, ca), vec3_mul(right, -sa));
        Vec3 rx_s = vec3_mul(rx, s);
        Vec3 uy_s = vec3_mul(uy, s);
        Vec3 a = vec3_sub(vec3_sub(p->pos, rx_s), uy_s);
        Vec3 b = vec3_sub(vec3_add(p->pos, rx_s), uy_s);
        Vec3 c = vec3_add(vec3_add(p->pos, rx_s), uy_s);
        Vec3 d = vec3_add(vec3_sub(p->pos, rx_s), uy_s);
        glColor3f(p->r, p->g, p->b);
        glBegin(GL_QUADS);
        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
        glVertex3f(c.x, c.y, c.z);
        glVertex3f(d.x, d.y, d.z);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    (void)eye;
}

void renderer_dropped_item(Renderer *r, uint8_t block, Vec3 pos, float rot, float age) {
    if (block == BLOCK_AIR || block >= BLOCK_COUNT)
        return;
    float bob = sinf(age * 3.5f) * 0.05f;
    float s = 0.22f;
    float hs = s * 0.5f;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->atlas_tex);
#if MCED_USE_GL
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
#endif
    glDisable(GL_BLEND);

    const BlockDef *def = &g_block_defs[block];
    bool is_flat = def->is_item || !def->solid || def->shape == SHAPE_CROSS ||
                   def->shape == SHAPE_TORCH || def->shape == SHAPE_FLAT_FLOOR ||
                   def->shape == SHAPE_DOOR;

    glPushMatrix();
    glTranslatef(pos.x, pos.y + 0.14f + bob, pos.z);
    glRotatef(rot, 0, 1, 0);

    if (is_flat) {
        glDisable(GL_CULL_FACE);
        uint16_t tile = g_block_tiles[block][FACE_TOP];
        float u0, v0, u1, v1;
        assets_tile_uv(r->assets, tile, &u0, &v0, &u1, &v1);
        float cr = (def->tint_mode != TINT_NONE) ? def->tint_r : 1.0f;
        float cg = (def->tint_mode != TINT_NONE) ? def->tint_g : 1.0f;
        float cb = (def->tint_mode != TINT_NONE) ? def->tint_b : 1.0f;
        glColor3f(cr, cg, cb);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex3f(-hs, hs, 0.0f);
        glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, 0.0f);
        glTexCoord2f(u1, v1); glVertex3f(hs, -hs, 0.0f);
        glTexCoord2f(u1, v0); glVertex3f(hs, hs, 0.0f);

        glTexCoord2f(u1, v0); glVertex3f(hs, hs, 0.0f);
        glTexCoord2f(u1, v1); glVertex3f(hs, -hs, 0.0f);
        glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, 0.0f);
        glTexCoord2f(u0, v0); glVertex3f(-hs, hs, 0.0f);
        glEnd();
        glPopMatrix();
        glEnable(GL_CULL_FACE);
#if MCED_USE_GL
        glDisable(GL_ALPHA_TEST);
#endif
        return;
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    for (int f = 0; f < FACE_COUNT; f++) {
        uint16_t tile = g_block_tiles[block][f];
        float u0, v0, u1, v1;
        assets_tile_uv(r->assets, tile, &u0, &v0, &u1, &v1);
        float sh = (f == FACE_TOP) ? 1.0f : (f == FACE_BOTTOM ? 0.5f : (f == FACE_NORTH || f == FACE_SOUTH ? 0.8f : 0.65f));
        glColor3f(sh, sh, sh);
        glBegin(GL_QUADS);
        switch (f) {
        case FACE_TOP:
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, -hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, hs, hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, hs, hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, hs, -hs);
            break;
        case FACE_BOTTOM:
            glTexCoord2f(u0, v0); glVertex3f(-hs, -hs, hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, -hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, -hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, -hs, hs);
            break;
        case FACE_NORTH:
            glTexCoord2f(u0, v0); glVertex3f(hs, hs, -hs);
            glTexCoord2f(u0, v1); glVertex3f(hs, -hs, -hs);
            glTexCoord2f(u1, v1); glVertex3f(-hs, -hs, -hs);
            glTexCoord2f(u1, v0); glVertex3f(-hs, hs, -hs);
            break;
        case FACE_SOUTH:
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, hs, hs);
            break;
        case FACE_WEST:
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, -hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, -hs);
            glTexCoord2f(u1, v1); glVertex3f(-hs, -hs, hs);
            glTexCoord2f(u1, v0); glVertex3f(-hs, hs, hs);
            break;
        case FACE_EAST:
            glTexCoord2f(u0, v0); glVertex3f(hs, hs, hs);
            glTexCoord2f(u0, v1); glVertex3f(hs, -hs, hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, -hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, hs, -hs);
            break;
        }
        glEnd();
    }
    glPopMatrix();
    glDisable(GL_CULL_FACE);
}

void renderer_tnt(Renderer *r, Vec3 pos, float fuse) {
    float s = 0.98f;
    float hs = s * 0.5f;
    bool flash = ((int)(fuse * 10.0f)) & 1;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->atlas_tex);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glPushMatrix();
    glTranslatef(pos.x, pos.y + hs, pos.z);

    for (int f = 0; f < FACE_COUNT; f++) {
        uint16_t tile = g_block_tiles[BLOCK_TNT][f];
        float u0, v0, u1, v1;
        assets_tile_uv(r->assets, tile, &u0, &v0, &u1, &v1);
        float sh = (f == FACE_TOP) ? 1.0f : (f == FACE_BOTTOM ? 0.5f : (f == FACE_NORTH || f == FACE_SOUTH ? 0.8f : 0.65f));
        if (flash) {
            glColor3f(1.0f, 1.0f, 1.0f);
        } else {
            glColor3f(sh, sh, sh);
        }
        glBegin(GL_QUADS);
        switch (f) {
        case FACE_TOP:
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, -hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, hs, hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, hs, hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, hs, -hs);
            break;
        case FACE_BOTTOM:
            glTexCoord2f(u0, v0); glVertex3f(-hs, -hs, hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, -hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, -hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, -hs, hs);
            break;
        case FACE_NORTH:
            glTexCoord2f(u0, v0); glVertex3f(hs, hs, -hs);
            glTexCoord2f(u0, v1); glVertex3f(hs, -hs, -hs);
            glTexCoord2f(u1, v1); glVertex3f(-hs, -hs, -hs);
            glTexCoord2f(u1, v0); glVertex3f(-hs, hs, -hs);
            break;
        case FACE_SOUTH:
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, hs, hs);
            break;
        case FACE_WEST:
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, -hs);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, -hs);
            glTexCoord2f(u1, v1); glVertex3f(-hs, -hs, hs);
            glTexCoord2f(u1, v0); glVertex3f(-hs, hs, hs);
            break;
        case FACE_EAST:
            glTexCoord2f(u0, v0); glVertex3f(hs, hs, hs);
            glTexCoord2f(u0, v1); glVertex3f(hs, -hs, hs);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, -hs);
            glTexCoord2f(u1, v0); glVertex3f(hs, hs, -hs);
            break;
        }
        glEnd();
    }
    glPopMatrix();
    glDisable(GL_CULL_FACE);
}

static void draw_steve_arm_box(Renderer *r, float hw, float hd, float len) {
    /* Steve's right arm (4x12x4 in 64x64 skin)
       Top:    (44, 16) to (48, 20)
       Bottom: (48, 16) to (52, 20)
       Right:  (40, 20) to (44, 32)
       Front:  (44, 20) to (48, 32)
       Left:   (48, 20) to (52, 32)
       Back:   (52, 20) to (56, 32) */
    const float inv = 1.0f / 64.0f;
    float u_top0 = 44.0f * inv, v_top0 = 16.0f * inv, u_top1 = 48.0f * inv, v_top1 = 20.0f * inv;
    float u_bot0 = 48.0f * inv, v_bot0 = 16.0f * inv, u_bot1 = 52.0f * inv, v_bot1 = 20.0f * inv;
    float u_rt0  = 40.0f * inv, v_rt0  = 20.0f * inv, u_rt1  = 44.0f * inv, v_rt1  = 32.0f * inv;
    float u_fr0  = 44.0f * inv, v_fr0  = 20.0f * inv, u_fr1  = 48.0f * inv, v_fr1  = 32.0f * inv;
    float u_lt0  = 48.0f * inv, v_lt0  = 20.0f * inv, u_lt1  = 52.0f * inv, v_lt1  = 32.0f * inv;
    float u_bk0  = 52.0f * inv, v_bk0  = 20.0f * inv, u_bk1  = 56.0f * inv, v_bk1  = 32.0f * inv;

    if (r->steve_tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, r->steve_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    glBegin(GL_QUADS);

    /* Top / Shoulder (+Y) */
    glColor3f(1.0f, 1.0f, 1.0f);
    glTexCoord2f(u_top0, v_top0); glVertex3f(-hw, 0.0f, -hd);
    glTexCoord2f(u_top0, v_top1); glVertex3f(-hw, 0.0f,  hd);
    glTexCoord2f(u_top1, v_top1); glVertex3f( hw, 0.0f,  hd);
    glTexCoord2f(u_top1, v_top0); glVertex3f( hw, 0.0f, -hd);

    /* Bottom / Hand Palm (-Y) */
    glColor3f(0.55f, 0.55f, 0.55f);
    glTexCoord2f(u_bot0, v_bot0); glVertex3f(-hw, -len,  hd);
    glTexCoord2f(u_bot0, v_bot1); glVertex3f(-hw, -len, -hd);
    glTexCoord2f(u_bot1, v_bot1); glVertex3f( hw, -len, -hd);
    glTexCoord2f(u_bot1, v_bot0); glVertex3f( hw, -len,  hd);

    /* Front face (+Z) */
    glColor3f(0.85f, 0.85f, 0.85f);
    glTexCoord2f(u_fr0, v_fr0); glVertex3f(-hw,  0.0f, hd);
    glTexCoord2f(u_fr0, v_fr1); glVertex3f(-hw, -len,  hd);
    glTexCoord2f(u_fr1, v_fr1); glVertex3f( hw, -len,  hd);
    glTexCoord2f(u_fr1, v_fr0); glVertex3f( hw,  0.0f, hd);

    /* Back face (-Z) */
    glColor3f(0.80f, 0.80f, 0.80f);
    glTexCoord2f(u_bk0, v_bk0); glVertex3f( hw,  0.0f, -hd);
    glTexCoord2f(u_bk0, v_bk1); glVertex3f( hw, -len,  -hd);
    glTexCoord2f(u_bk1, v_bk1); glVertex3f(-hw, -len,  -hd);
    glTexCoord2f(u_bk1, v_bk0); glVertex3f(-hw,  0.0f, -hd);

    /* Left / Inside face (-X) */
    glColor3f(0.70f, 0.70f, 0.70f);
    glTexCoord2f(u_lt0, v_lt0); glVertex3f(-hw,  0.0f, -hd);
    glTexCoord2f(u_lt0, v_lt1); glVertex3f(-hw, -len,  -hd);
    glTexCoord2f(u_lt1, v_lt1); glVertex3f(-hw, -len,   hd);
    glTexCoord2f(u_lt1, v_lt0); glVertex3f(-hw,  0.0f,  hd);

    /* Right / Outside face (+X) */
    glColor3f(0.65f, 0.65f, 0.65f);
    glTexCoord2f(u_rt0, v_rt0); glVertex3f(hw,  0.0f,  hd);
    glTexCoord2f(u_rt0, v_rt1); glVertex3f(hw, -len,   hd);
    glTexCoord2f(u_rt1, v_rt1); glVertex3f(hw, -len,  -hd);
    glTexCoord2f(u_rt1, v_rt0); glVertex3f(hw,  0.0f, -hd);

    glEnd();
}

void renderer_hand(Renderer *r, uint8_t held_block, float swing_timer, float bob, bool in_water) {
    (void)in_water;
    if (held_block >= BLOCK_COUNT)
        held_block = BLOCK_AIR;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    float fov = 65.0f;
    float aspect = (float)r->width / (float)r->height;
    float zNear = 0.05f, zFar = 10.0f;
    float top = zNear * tanf(fov * 0.5f * MCED_DEG2RAD);
    float bottom = -top;
    float left = bottom * aspect;
    float right = top * aspect;
    glFrustum(left, right, bottom, top, zNear, zFar);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    float swing = sinf((swing_timer / 0.25f) * (float)M_PI);
    float bob_x = cosf(bob) * 0.015f;
    float bob_y = fabsf(sinf(bob)) * 0.018f;

    if (held_block == BLOCK_AIR) {
        /* Empty hand: Steve's right arm punching */
        glPushMatrix();
        glTranslatef(0.38f + bob_x - swing * 0.15f, -0.22f + bob_y - swing * 0.20f, -0.42f - swing * 0.18f);
        glRotatef(45.0f - swing * 35.0f, 1, 0, 0);
        glRotatef(-35.0f - swing * 30.0f, 0, 1, 0);
        glRotatef(20.0f + swing * 25.0f, 0, 0, 1);
        draw_steve_arm_box(r, 0.065f, 0.065f, 0.45f);
        glPopMatrix();
    } else {
        const BlockDef *def = &g_block_defs[held_block];
        bool is_flat = def->is_item || !def->solid || def->shape == SHAPE_CROSS ||
                       def->shape == SHAPE_TORCH || def->shape == SHAPE_FLAT_FLOOR ||
                       def->shape == SHAPE_DOOR;

        /* Forearm holding item / block */
        glPushMatrix();
        glTranslatef(0.36f + bob_x - swing * 0.12f, -0.28f + bob_y - swing * 0.18f, -0.44f - swing * 0.14f);
        glRotatef(38.0f - swing * 40.0f, 1, 0, 0);
        glRotatef(-32.0f - swing * 25.0f, 0, 1, 0);
        glRotatef(12.0f, 0, 0, 1);
        draw_steve_arm_box(r, 0.050f, 0.050f, 0.38f);
        glPopMatrix();

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, r->atlas_tex);
#if MCED_USE_GL
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.1f);
#endif

        if (is_flat) {
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glPushMatrix();
            glTranslatef(0.30f + bob_x - swing * 0.15f, -0.15f + bob_y - swing * 0.18f, -0.42f - swing * 0.12f);
            glRotatef(40.0f - swing * 55.0f, 1, 0, 0);
            glRotatef(-40.0f + swing * 30.0f, 0, 1, 0);
            glRotatef(15.0f, 0, 0, 1);

            float s = 0.30f;
            float hs = s * 0.5f;
            float u0, v0, u1, v1;
            uint16_t tile = g_block_tiles[held_block][FACE_TOP];
            assets_tile_uv(r->assets, tile, &u0, &v0, &u1, &v1);
            float cr = (def->tint_mode != TINT_NONE) ? def->tint_r : 1.0f;
            float cg = (def->tint_mode != TINT_NONE) ? def->tint_g : 1.0f;
            float cb = (def->tint_mode != TINT_NONE) ? def->tint_b : 1.0f;
            glColor3f(cr, cg, cb);
            glBegin(GL_QUADS);
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, 0.0f);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, 0.0f);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, 0.0f);
            glTexCoord2f(u1, v0); glVertex3f(hs, hs, 0.0f);

            glTexCoord2f(u1, v0); glVertex3f(hs, hs, 0.0f);
            glTexCoord2f(u1, v1); glVertex3f(hs, -hs, 0.0f);
            glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, 0.0f);
            glTexCoord2f(u0, v0); glVertex3f(-hs, hs, 0.0f);
            glEnd();
            glPopMatrix();
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
        } else {
            /* Render 3D mini block held in hand */
            glPushMatrix();
            glTranslatef(0.28f + bob_x - swing * 0.12f, -0.18f + bob_y - swing * 0.15f, -0.44f - swing * 0.10f);
            glRotatef(25.0f - swing * 45.0f, 1, 0, 0);
            glRotatef(-35.0f + swing * 25.0f, 0, 1, 0);
            glRotatef(12.0f, 0, 0, 1);

            float s = 0.22f;
            float hs = s * 0.5f;
            for (int f = 0; f < FACE_COUNT; f++) {
                uint16_t tile = g_block_tiles[held_block][f];
                float u0, v0, u1, v1;
                assets_tile_uv(r->assets, tile, &u0, &v0, &u1, &v1);
                float sh = (f == FACE_TOP) ? 1.0f : (f == FACE_BOTTOM ? 0.5f : (f == FACE_NORTH || f == FACE_SOUTH ? 0.8f : 0.65f));
                float r_col = sh, g_col = sh, b_col = sh;
                if (def->tint_mode == TINT_ALL || (def->tint_mode == TINT_TOP && f == FACE_TOP)) {
                    r_col *= def->tint_r;
                    g_col *= def->tint_g;
                    b_col *= def->tint_b;
                }
                glColor3f(r_col, g_col, b_col);
                glBegin(GL_QUADS);
                switch (f) {
                case FACE_TOP:
                    glTexCoord2f(u0, v0); glVertex3f(-hs, hs, -hs);
                    glTexCoord2f(u0, v1); glVertex3f(-hs, hs, hs);
                    glTexCoord2f(u1, v1); glVertex3f(hs, hs, hs);
                    glTexCoord2f(u1, v0); glVertex3f(hs, hs, -hs);
                    break;
                case FACE_BOTTOM:
                    glTexCoord2f(u0, v0); glVertex3f(-hs, -hs, hs);
                    glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, -hs);
                    glTexCoord2f(u1, v1); glVertex3f(hs, -hs, -hs);
                    glTexCoord2f(u1, v0); glVertex3f(hs, -hs, hs);
                    break;
                case FACE_NORTH:
                    glTexCoord2f(u0, v0); glVertex3f(hs, hs, -hs);
                    glTexCoord2f(u0, v1); glVertex3f(hs, -hs, -hs);
                    glTexCoord2f(u1, v1); glVertex3f(-hs, -hs, -hs);
                    glTexCoord2f(u1, v0); glVertex3f(-hs, hs, -hs);
                    break;
                case FACE_SOUTH:
                    glTexCoord2f(u0, v0); glVertex3f(-hs, hs, hs);
                    glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, hs);
                    glTexCoord2f(u1, v1); glVertex3f(hs, -hs, hs);
                    glTexCoord2f(u1, v0); glVertex3f(hs, hs, hs);
                    break;
                case FACE_WEST:
                    glTexCoord2f(u0, v0); glVertex3f(-hs, hs, -hs);
                    glTexCoord2f(u0, v1); glVertex3f(-hs, -hs, -hs);
                    glTexCoord2f(u1, v1); glVertex3f(-hs, -hs, hs);
                    glTexCoord2f(u1, v0); glVertex3f(-hs, hs, hs);
                    break;
                case FACE_EAST:
                    glTexCoord2f(u0, v0); glVertex3f(hs, hs, hs);
                    glTexCoord2f(u0, v1); glVertex3f(hs, -hs, hs);
                    glTexCoord2f(u1, v1); glVertex3f(hs, -hs, -hs);
                    glTexCoord2f(u1, v0); glVertex3f(hs, hs, -hs);
                    break;
                }
                glEnd();
            }
            glPopMatrix();
        }
#if MCED_USE_GL
        glDisable(GL_ALPHA_TEST);
#endif
    }

    glDisable(GL_CULL_FACE);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

/* ------------------------------------------------------------------ */
/* UI drawing                                                          */
/* ------------------------------------------------------------------ */

static void tex_quad(Renderer *r, GLuint tex, float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1, Color3 c, bool additive) {
    (void)r;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (additive) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glColor3f(c.r, c.g, c.b);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u0, v1); glVertex2f(x, y + h);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glEnd();
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void renderer_menu_background(Renderer *r) {
    if (!r->panorama_tex[0]) {
        if (!r->menu_tex)
            return;
        float w = (float)(r->ui_width > 0 ? r->ui_width : r->width);
        float h = (float)(r->ui_height > 0 ? r->ui_height : r->height);
        float v0 = 0.21875f, v1 = 0.78125f;
        tex_quad(r, r->menu_tex, 0, 0, w, h,
                 0, v0, 1, v1, (Color3){1, 1, 1}, false);
        return;
    }

    /* 1. Setup 3D camera projection for the rotating skybox */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    float fov = 85.0f;
    float aspect = (float)r->width / (float)(r->height > 0 ? r->height : 1);
    float zNear = 0.05f, zFar = 10.0f;
    float top = zNear * tanf(fov * 0.5f * MCED_DEG2RAD);
    float bottom = -top;
    float left = bottom * aspect;
    float right = top * aspect;
    glFrustum(left, right, bottom, top, zNear, zFar);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
#if MCED_USE_GL
    glDisable(GL_ALPHA_TEST);
#endif

    /* Rotation matching vanilla Minecraft Java Edition */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    float time = (float)ts.tv_sec + (float)ts.tv_nsec * 1e-9f;
    float yaw = time * 4.5f;
    float pitch = sinf(time * 0.35f) * 4.0f;

    glRotatef(pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(yaw, 0.0f, 1.0f, 0.0f);

    glColor3f(1.0f, 1.0f, 1.0f);

    /* Face 0: South (+Z) */
    glBindTexture(GL_TEXTURE_2D, r->panorama_tex[0]);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, -1.0f, 1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f, -1.0f, 1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,  1.0f, 1.0f);
    glEnd();

    /* Face 1: West / +X */
    glBindTexture(GL_TEXTURE_2D, r->panorama_tex[1]);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(1.0f,  1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(1.0f, -1.0f,  1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(1.0f,  1.0f, -1.0f);
    glEnd();

    /* Face 2: North (-Z) */
    glBindTexture(GL_TEXTURE_2D, r->panorama_tex[2]);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    glEnd();

    /* Face 3: East / -X */
    glBindTexture(GL_TEXTURE_2D, r->panorama_tex[3]);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
    glEnd();

    /* Face 4: Top (+Y) */
    glBindTexture(GL_TEXTURE_2D, r->panorama_tex[4]);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, 1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, 1.0f,  1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f, 1.0f,  1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, 1.0f, -1.0f);
    glEnd();

    /* Face 5: Bottom (-Y) */
    glBindTexture(GL_TEXTURE_2D, r->panorama_tex[5]);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
    glEnd();

    /* Restore matrices and state */
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glDepthMask(GL_TRUE);

    /* 2. Soft gradient vignette overlay (matching vanilla title screen) */
    float w = (float)(r->ui_width > 0 ? r->ui_width : r->width);
    float h = (float)(r->ui_height > 0 ? r->ui_height : r->height);

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.0f, 0.0f, 0.0f, 0.18f);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(w, 0.0f);
    glColor4f(0.0f, 0.0f, 0.0f, 0.65f);
    glVertex2f(w, h);
    glVertex2f(0.0f, h);
    glEnd();

    glDisable(GL_BLEND);
}

void renderer_dirt_background(Renderer *r) {
    float w = (float)(r->ui_width > 0 ? r->ui_width : r->width);
    float h = (float)(r->ui_height > 0 ? r->ui_height : r->height);
    if (r->dirt_bg_tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, r->dirt_bg_tex);
        glColor3f(0.25f, 0.25f, 0.25f);
        float u_max = w / 32.0f;
        float v_max = h / 32.0f;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0, 0);
        glTexCoord2f(u_max, 0.0f); glVertex2f(w, 0);
        glTexCoord2f(u_max, v_max); glVertex2f(w, h);
        glTexCoord2f(0.0f, v_max); glVertex2f(0, h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        renderer_rect(r, 0, 0, w, h, (Color3){0.12f, 0.09f, 0.07f});
    }
}

void renderer_button(Renderer *r, float x, float y, float w, float h,
                     const char *label, bool hover, bool disabled) {
    GLuint tex = disabled ? (r->button_disabled_tex ? r->button_disabled_tex : r->button_tex) :
                 (hover ? r->button_hl_tex : r->button_tex);
    if (tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glColor4f(1, 1, 1, 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        float cap_w = 4.0f;
        float u_cap = 4.0f / 200.0f;

        glBegin(GL_QUADS);
        /* Left cap */
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
        glTexCoord2f(u_cap, 0.0f); glVertex2f(x + cap_w, y);
        glTexCoord2f(u_cap, 1.0f); glVertex2f(x + cap_w, y + h);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + h);

        /* Center */
        glTexCoord2f(u_cap, 0.0f); glVertex2f(x + cap_w, y);
        glTexCoord2f(1.0f - u_cap, 0.0f); glVertex2f(x + w - cap_w, y);
        glTexCoord2f(1.0f - u_cap, 1.0f); glVertex2f(x + w - cap_w, y + h);
        glTexCoord2f(u_cap, 1.0f); glVertex2f(x + cap_w, y + h);

        /* Right cap */
        glTexCoord2f(1.0f - u_cap, 0.0f); glVertex2f(x + w - cap_w, y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
        glTexCoord2f(1.0f - u_cap, 1.0f); glVertex2f(x + w - cap_w, y + h);
        glEnd();

        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
    } else {
        Color3 fill = disabled ? (Color3){0.20f, 0.20f, 0.22f} :
                      (hover ? (Color3){0.42f, 0.42f, 0.47f} : (Color3){0.28f, 0.28f, 0.32f});
        renderer_rect(r, x, y, w, h, (Color3){0.1f, 0.1f, 0.1f});
        renderer_rect(r, x + 1, y + 1, w - 2, h - 2, fill);
        renderer_rect(r, x + 1, y + 1, w - 2, 1, (Color3){0.55f, 0.55f, 0.60f});
        renderer_rect(r, x + 1, y + h - 2, w - 2, 1, (Color3){0.18f, 0.18f, 0.20f});
    }

    if (label && label[0]) {
        Color3 tc = disabled ? (Color3){0.65f, 0.65f, 0.65f} :
                    (hover ? (Color3){1.0f, 1.0f, 0.62f} : (Color3){0.88f, 0.88f, 0.88f});
        renderer_text_centered(r, x + w * 0.5f, y + (h - 8) * 0.5f - 1.0f,
                               1.15f, label, tc, true);
    }
}

void renderer_slider(Renderer *r, float x, float y, float w, float h,
                     float t, const char *label, bool hover) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    GLuint bg_tex = hover ? (r->slider_hl_tex ? r->slider_hl_tex : r->slider_tex) : r->slider_tex;
    if (bg_tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, bg_tex);
        glColor4f(1, 1, 1, 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        float cap_w = 4.0f;
        float u_cap = 4.0f / 200.0f;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
        glTexCoord2f(u_cap, 0.0f); glVertex2f(x + cap_w, y);
        glTexCoord2f(u_cap, 1.0f); glVertex2f(x + cap_w, y + h);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + h);

        glTexCoord2f(u_cap, 0.0f); glVertex2f(x + cap_w, y);
        glTexCoord2f(1.0f - u_cap, 0.0f); glVertex2f(x + w - cap_w, y);
        glTexCoord2f(1.0f - u_cap, 1.0f); glVertex2f(x + w - cap_w, y + h);
        glTexCoord2f(u_cap, 1.0f); glVertex2f(x + cap_w, y + h);

        glTexCoord2f(1.0f - u_cap, 0.0f); glVertex2f(x + w - cap_w, y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
        glTexCoord2f(1.0f - u_cap, 1.0f); glVertex2f(x + w - cap_w, y + h);
        glEnd();
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
    } else {
        renderer_rect(r, x, y, w, h, (Color3){0.12f, 0.12f, 0.14f});
        renderer_rect(r, x + 1, y + 1, w - 2, h - 2, (Color3){0.18f, 0.18f, 0.22f});
    }

    float hw = 8.0f;
    float hx = x + t * (w - hw);
    GLuint handle_tex = hover ? (r->slider_handle_hl_tex ? r->slider_handle_hl_tex : r->slider_handle_tex) : r->slider_handle_tex;
    if (handle_tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, handle_tex);
        glColor4f(1, 1, 1, 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(hx, y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(hx + hw, y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(hx + hw, y + h);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(hx, y + h);
        glEnd();
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
    } else {
        renderer_rect(r, hx, y, hw, h, (Color3){0.1f, 0.1f, 0.1f});
        renderer_rect(r, hx + 1, y + 1, hw - 2, h - 2, hover ? (Color3){0.5f, 0.5f, 0.55f} : (Color3){0.35f, 0.35f, 0.4f});
    }

    if (label && label[0]) {
        Color3 tc = hover ? (Color3){1.0f, 1.0f, 0.62f} : (Color3){0.88f, 0.88f, 0.88f};
        renderer_text_centered(r, x + w * 0.5f, y + (h - 8) * 0.5f - 1.0f,
                               1.15f, label, tc, true);
    }
}

void renderer_logo(Renderer *r, float x, float y, float w, float h) {
    if (r->logo_tex) {
        tex_quad(r, r->logo_tex, x, y, w, h, 0, 0, 1, 0.25f,
                 (Color3){1, 1, 1}, true);
        return;
    }
    renderer_text_centered(r, x + w * 0.5f, y + 8, 4.0f, "MINECRAFT", (Color3){1, 1, 1}, true);
    renderer_text_centered(r, x + w * 0.5f, y + 48, 2.0f, "C EDITION", (Color3){1, 0.85f, 0.2f}, true);
}

void renderer_rect(Renderer *r, float x, float y, float w, float h, Color3 c) {
    (void)r;
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glColor3f(c.r, c.g, c.b);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x, y + h);
    glVertex2f(x + w, y + h);
    glVertex2f(x + w, y);
    glEnd();
}

void renderer_rect_outline(Renderer *r, float x, float y, float w, float h,
                           float t, Color3 c) {
    renderer_rect(r, x, y, w, t, c);
    renderer_rect(r, x, y + h - t, w, t, c);
    renderer_rect(r, x, y + t, t, h - 2 * t, c);
    renderer_rect(r, x + w - t, y + t, t, h - 2 * t, c);
}

void renderer_texture_quad(Renderer *r, GLuint tex, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1, Color3 c) {
    tex_quad(r, tex, x, y, w, h, u0, v0, u1, v1, c, false);
}

void renderer_sprite_uv(Renderer *r, GLuint tex, float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1) {
    if (!tex) return;
    tex_quad(r, tex, x, y, w, h, u0, v0, u1, v1, (Color3){1, 1, 1}, false);
}

void renderer_sprite(Renderer *r, GLuint tex, float x, float y, float w, float h) {
    renderer_sprite_uv(r, tex, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
}

/* Return the number of visible columns in a glyph cell. */
static int glyph_width(const Assets *a, unsigned char ch) {
    if (!a->has_font)
        return 5;
    if (ch == ' ')
        return 3;
    int idx = (int)ch - 32;
    if (idx < 0 || idx > 95)
        idx = (int)'?' - 32;
    int cx = (idx % 16) * 8;
    /* The vanilla sheet starts with the 32 control characters, so
       printable ASCII begins two 8px rows down. */
    int cy = 16 + (idx / 16) * 8;
    int right = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (a->font_alpha[(cy + y) * 128 + cx + x] > 16 && x + 1 > right)
                right = x + 1;
        }
    }
    if (right < 1)
        right = 1;
    return right;
}

static int glyph_advance(const Assets *a, unsigned char ch) {
    int w = glyph_width(a, ch);
    return w + 1;
}

static int text_scale_int(float scale) {
    int s = (int)lroundf(scale);
    return s < 1 ? 1 : s;
}

int renderer_text_width(Renderer *r, float scale, const char *text) {
    int total = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        total += glyph_advance(r->assets, *p);
    /* Must match renderer_text(), which rounds the scale to an integer. */
    return total * text_scale_int(scale);
}

static void draw_glyph(Renderer *r, unsigned char ch, float x, float y, float scale,
                       Color3 c) {
    int idx = (int)ch - 32;
    if (idx < 0 || idx > 95)
        idx = (int)'?' - 32;
    int cx = (idx % 16) * 8;
    int cy = 16 + (idx / 16) * 8;
    int w = glyph_width(r->assets, ch);
    /* Stay half a texel inside the cell.  TinyGL's triangle rasterizer
       draws an extra scanline on the bottom edge, which would otherwise
       sample the first row of the next glyph. */
    float u0 = (float)(cx * 8 + 0.5f) / (float)MCED_ATLAS_SIZE;
    float u1 = (float)((cx + w) * 8 - 0.5f) / (float)MCED_ATLAS_SIZE;
    float v0 = (float)(cy * 8 + 0.5f) / (float)MCED_ATLAS_SIZE;
    float v1 = (float)((cy + 8) * 8 - 0.5f) / (float)MCED_ATLAS_SIZE;
    glColor3f(c.r, c.g, c.b);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u0, v1); glVertex2f(x, y + 8 * scale);
    glTexCoord2f(u1, v1); glVertex2f(x + w * scale, y + 8 * scale);
    glTexCoord2f(u1, v0); glVertex2f(x + w * scale, y);
    glEnd();
    (void)r;
}

void renderer_text(Renderer *r, float x, float y, float scale, const char *text,
                   Color3 c, bool shadow) {
    if (!text || !*text)
        return;
    int s = text_scale_int(scale);
    x = roundf(x);
    y = roundf(y);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->font_tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_DEPTH_TEST);

    if (shadow) {
        float sx = x + s, sy = y + s;
        Color3 sc = {c.r * 0.20f, c.g * 0.20f, c.b * 0.20f};
        for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
            draw_glyph(r, *p, sx, sy, (float)s, sc);
            sx += glyph_advance(r->assets, *p) * s;
        }
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        draw_glyph(r, *p, x, y, (float)s, c);
        x += glyph_advance(r->assets, *p) * s;
    }

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void renderer_text_centered(Renderer *r, float cx, float y, float scale,
                            const char *text, Color3 c, bool shadow) {
    int s = text_scale_int(scale);
    int total = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        total += glyph_advance(r->assets, *p);
    float w = (float)(total * s);
    renderer_text(r, roundf(cx - w * 0.5f), y, (float)s, text, c, shadow);
}

void renderer_block_icon(Renderer *r, uint8_t block, float x, float y, float size) {
    if (block == BLOCK_AIR || block >= BLOCK_COUNT)
        return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->atlas_tex);

    const BlockDef *def = &g_block_defs[block];
    bool is_flat = def->is_item || def->shape == SHAPE_CROSS ||
                   def->shape == SHAPE_TORCH || def->shape == SHAPE_FLAT_FLOOR ||
                   def->shape == SHAPE_DOOR;
    float u0, v0, u1, v1;
    if (is_flat) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if MCED_USE_GL
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.1f);
#endif
        assets_tile_uv(r->assets, g_block_tiles[block][FACE_TOP], &u0, &v0, &u1, &v1);
        float cr = (def->tint_mode != TINT_NONE) ? def->tint_r : 1.0f;
        float cg = (def->tint_mode != TINT_NONE) ? def->tint_g : 1.0f;
        float cb = (def->tint_mode != TINT_NONE) ? def->tint_b : 1.0f;
        glColor4f(cr, cg, cb, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(x, y);
        glTexCoord2f(u1, v0); glVertex2f(x + size, y);
        glTexCoord2f(u1, v1); glVertex2f(x + size, y + size);
        glTexCoord2f(u0, v1); glVertex2f(x, y + size);
        glEnd();
#if MCED_USE_GL
        glDisable(GL_ALPHA_TEST);
#endif
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if MCED_USE_GL
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
#endif

    float cx = x + size * 0.5f;
    float w  = size * 0.42f;
    float h  = (def->shape == SHAPE_SLAB) ? (size * 0.20f) : (size * 0.40f);
    float cy = (def->shape == SHAPE_SLAB) ? (y + size * 0.62f) : (y + size * 0.52f);
    float top_dy = size * 0.22f;

    /* Top face */
    float top_r = 1.0f, top_g = 1.0f, top_b = 1.0f;
    if (def->tint_mode == TINT_ALL || def->tint_mode == TINT_TOP) {
        top_r = def->tint_r; top_g = def->tint_g; top_b = def->tint_b;
    }
    assets_tile_uv(r->assets, g_block_tiles[block][FACE_TOP], &u0, &v0, &u1, &v1);
    glColor4f(top_r, top_g, top_b, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(cx, cy - top_dy * 2.0f);
    glTexCoord2f(u1, v0); glVertex2f(cx + w, cy - top_dy);
    glTexCoord2f(u1, v1); glVertex2f(cx, cy);
    glTexCoord2f(u0, v1); glVertex2f(cx - w, cy - top_dy);
    glEnd();

    /* Left face (South / facing front-left) */
    float left_r = 0.80f, left_g = 0.80f, left_b = 0.80f;
    if (def->tint_mode == TINT_ALL) {
        left_r *= def->tint_r; left_g *= def->tint_g; left_b *= def->tint_b;
    }
    assets_tile_uv(r->assets, g_block_tiles[block][FACE_SOUTH], &u0, &v0, &u1, &v1);
    glColor4f(left_r, left_g, left_b, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(cx - w, cy - top_dy);
    glTexCoord2f(u1, v0); glVertex2f(cx, cy);
    glTexCoord2f(u1, v1); glVertex2f(cx, cy + h);
    glTexCoord2f(u0, v1); glVertex2f(cx - w, cy - top_dy + h);
    glEnd();

    /* Right face (East / facing front-right) */
    float right_r = 0.60f, right_g = 0.60f, right_b = 0.60f;
    if (def->tint_mode == TINT_ALL) {
        right_r *= def->tint_r; right_g *= def->tint_g; right_b *= def->tint_b;
    }
    assets_tile_uv(r->assets, g_block_tiles[block][FACE_EAST], &u0, &v0, &u1, &v1);
    glColor4f(right_r, right_g, right_b, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(cx, cy);
    glTexCoord2f(u1, v0); glVertex2f(cx + w, cy - top_dy);
    glTexCoord2f(u1, v1); glVertex2f(cx + w, cy - top_dy + h);
    glTexCoord2f(u0, v1); glVertex2f(cx, cy + h);
    glEnd();

#if MCED_USE_GL
    glDisable(GL_ALPHA_TEST);
#endif
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

/* ------------------------------------------------------------------ */
/* Screenshots                                                         */
/* ------------------------------------------------------------------ */

bool renderer_screenshot(Renderer *r, const char *path) {
#if MCED_USE_GL
    int w = r->width, h = r->height;
    uint8_t *rgb = xmalloc((size_t)w * h * 3);
    uint8_t *row_tmp = xmalloc((size_t)w * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    /* OpenGL glReadPixels is bottom-up, invert rows vertically */
    for (int y = 0; y < h / 2; y++) {
        uint8_t *top = rgb + (size_t)y * w * 3;
        uint8_t *bot = rgb + (size_t)(h - 1 - y) * w * 3;
        memcpy(row_tmp, top, (size_t)w * 3);
        memcpy(top, bot, (size_t)w * 3);
        memcpy(bot, row_tmp, (size_t)w * 3);
    }
    free(row_tmp);
    int ok = stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);
    if (ok)
        printf("screenshot: %s (%dx%d)\n", path, w, h);
    else
        fprintf(stderr, "failed to write screenshot: %s\n", path);
    return ok != 0;
#else
    if (!r->zb || !r->zb->pbuf)
        return false;
    int w = r->zb->xsize, h = r->zb->ysize;
    uint8_t *rgb = xmalloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++) {
        const PIXEL *row = (const PIXEL *)((const uint8_t *)r->zb->pbuf +
                                           (size_t)y * r->zb->linesize);
        uint8_t *dst = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            PIXEL p = row[x];
            dst[x * 3 + 0] = (uint8_t)((p >> 16) & 0xff);
            dst[x * 3 + 1] = (uint8_t)((p >> 8) & 0xff);
            dst[x * 3 + 2] = (uint8_t)(p & 0xff);
        }
    }
    int ok = stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);
    if (ok)
        printf("screenshot: %s (%dx%d)\n", path, w, h);
    else
        fprintf(stderr, "failed to write screenshot: %s\n", path);
    return ok != 0;
#endif
}

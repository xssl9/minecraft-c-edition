#ifndef MCED_RENDER_H
#define MCED_RENDER_H

#include "assets.h"
#include "mced.h"
#include "world.h"

#if MCED_USE_GL
#include <GL/gl.h>
#else
#include "GL/gl.h"
#include "zbuffer.h"
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float x, y, z;
    float u, v;
    float r, g, b;
} RVertex;

typedef struct ChunkMesh ChunkMesh;

#define RENDER_MAX_PARTICLES 384

typedef struct {
    Vec3 pos;
    Vec3 vel;
    float life;
    float max_life;
    float size;
    float rot;
    float rot_speed;
    float r, g, b;
} RenderParticle;

typedef struct {
    int width, height;
    float ui_scale;
    int ui_width, ui_height;
#if !MCED_USE_GL
    ZBuffer *zb;
#endif
    const Assets *assets;

    GLuint atlas_tex;
    GLuint menu_tex;
    GLuint panorama_tex[6];
    GLuint logo_tex;
    GLuint font_tex;
    GLuint sun_tex;
    GLuint moon_tex;
    GLuint clouds_tex;
    GLuint button_tex;
    GLuint button_hl_tex;
    GLuint button_disabled_tex;
    GLuint slider_tex;
    GLuint slider_hl_tex;
    GLuint slider_handle_tex;
    GLuint slider_handle_hl_tex;
    GLuint dirt_bg_tex;
    GLuint adv_window_tex;
    GLuint adv_stone_tex;
    GLuint steve_tex;

    /* Authentic HUD and Container GUI textures */
    GLuint gui_hotbar_tex;
    GLuint gui_hotbar_sel_tex;
    GLuint gui_crosshair_tex;
    GLuint gui_exp_bg_tex;
    GLuint gui_exp_prog_tex;
    GLuint gui_heart_empty_tex;
    GLuint gui_heart_full_tex;
    GLuint gui_heart_half_tex;
    GLuint gui_heart_blink_tex;
    GLuint gui_heart_half_blink_tex;
    GLuint gui_food_empty_tex;
    GLuint gui_food_full_tex;
    GLuint gui_food_half_tex;
    GLuint gui_armor_empty_tex;
    GLuint gui_armor_full_tex;
    GLuint gui_armor_half_tex;
    GLuint gui_air_tex;
    GLuint gui_air_pop_tex;
    GLuint gui_inventory_tex;
    GLuint gui_creative_tex;
    GLuint gui_crafting_tex;
    GLuint gui_furnace_tex;
    GLuint gui_furnace_lit_tex;
    GLuint gui_furnace_burn_tex;
    GLuint gui_chest_tex;
    uint16_t break_stage_tiles[10];

    float fov;
    bool clouds_enabled;
    bool fog_enabled;
    bool view_bobbing;
    bool smooth_lighting;
    float brightness;

    ChunkMesh *chunks;
    int chunk_count;
    int chunk_capacity;
    ChunkMesh **mesh_hash;
    int mesh_hash_capacity;
    int render_radius;

    RVertex *scratch_verts;
    int scratch_verts_capacity;

    float fog_color[4];

    int last_quads;
    int last_chunks;
    int last_rebuilds;
    int total_rebuilds;
    int last_pcx, last_pcz;
    bool all_built;

    RenderParticle particles[RENDER_MAX_PARTICLES];
    int particle_count;
} Renderer;

bool renderer_init(Renderer *r, const Assets *assets, int width, int height);
void renderer_resize(Renderer *r, int width, int height);
void renderer_shutdown(Renderer *r);

void renderer_clear(Renderer *r);
void renderer_sky(Renderer *r, float world_time, Vec3 eye, float yaw, float pitch);
void renderer_clouds(Renderer *r, float world_time, Vec3 eye);
void renderer_world(Renderer *r, World *w, Vec3 eye, float yaw, float pitch, int rebuild_budget);
void renderer_warmup(Renderer *r, World *w, Vec3 eye);
void renderer_reset_chunks(Renderer *r);
void renderer_outline(Renderer *r, int bx, int by, int bz, bool visible);
void renderer_draw_break_progress(Renderer *r, int bx, int by, int bz, float progress);
void renderer_hand(Renderer *r, uint8_t held_block, float swing_timer, float bob, bool in_water);
void renderer_dropped_item(Renderer *r, uint8_t block, Vec3 pos, float rot, float age);
void renderer_tnt(Renderer *r, Vec3 pos, float fuse);

void renderer_particles_clear(Renderer *r);
void renderer_particles_break(Renderer *r, uint8_t block, int bx, int by, int bz);
void renderer_particles_place(Renderer *r, World *w, uint8_t block,
                              int bx, int by, int bz, int nx, int ny, int nz);
void renderer_particles_update(Renderer *r, World *w, float dt);
void renderer_particles_draw(Renderer *r, Vec3 eye, float yaw, float pitch);
void renderer_ui_begin(Renderer *r);

void renderer_menu_background(Renderer *r);
void renderer_dirt_background(Renderer *r);
void renderer_button(Renderer *r, float x, float y, float w, float h, const char *label, bool hover, bool disabled);
void renderer_slider(Renderer *r, float x, float y, float w, float h, float t, const char *label, bool hover);
void renderer_logo(Renderer *r, float x, float y, float w, float h);
void renderer_rect(Renderer *r, float x, float y, float w, float h, Color3 c);
void renderer_rect_outline(Renderer *r, float x, float y, float w, float h, float t, Color3 c);
void renderer_texture_quad(Renderer *r, GLuint tex, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1, Color3 c);
void renderer_sprite(Renderer *r, GLuint tex, float x, float y, float w, float h);
void renderer_sprite_uv(Renderer *r, GLuint tex, float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1);
void renderer_text(Renderer *r, float x, float y, float scale, const char *text, Color3 c, bool shadow);
void renderer_text_centered(Renderer *r, float cx, float y, float scale, const char *text, Color3 c, bool shadow);
int renderer_text_width(Renderer *r, float scale, const char *text);
void renderer_block_icon(Renderer *r, uint8_t block, float x, float y, float size);

bool renderer_screenshot(Renderer *r, const char *path);

#endif

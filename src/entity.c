#include "entity.h"
#include "blocks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MCED_USE_GL
#include <GL/gl.h>
#endif

void entity_manager_init(EntityManager *em, const Assets *assets) {
    memset(em, 0, sizeof(*em));
    em->next_id = 1;
    (void)assets;
}

Entity *entity_spawn(EntityManager *em, EntityType type, Vec3 pos) {
    if (type == ENTITY_NONE)
        return NULL;

    int slot = -1;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (!em->list[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return NULL;

    Entity *e = &em->list[slot];
    memset(e, 0, sizeof(*e));
    e->id = em->next_id++;
    e->type = type;
    e->pos = pos;
    e->active = true;

    if (type == ENTITY_ARROW) {
        e->width = 0.3f;
        e->height = 0.3f;
        e->health = 1.0f;
        e->max_health = 1.0f;
    }
    em->count++;
    return e;
}

static bool entity_collides(World *w, Vec3 pos, float width, float height) {
    float hw = width * 0.5f;
    const float e = 0.001f;
    int x0 = (int)floorf(pos.x - hw + e);
    int x1 = (int)floorf(pos.x + hw - e);
    int y0 = (int)floorf(pos.y + e);
    int y1 = (int)floorf(pos.y + height - e);
    int z0 = (int)floorf(pos.z - hw + e);
    int z1 = (int)floorf(pos.z + hw - e);
    for (int y = y0; y <= y1; y++) {
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                uint8_t b = world_get_block(w, x, y, z);
                if (b != BLOCK_AIR && block_is_solid(b))
                    return true;
            }
        }
    }
    return false;
}

static void entity_move(Entity *e, World *w, float dt) {
    if (e->type == ENTITY_ARROW) {
        e->pos = vec3_add(e->pos, vec3_mul(e->vel, dt));
        e->vel.y -= 12.0f * dt;
        return;
    }
}

void entity_update_all(EntityManager *em, World *w, Player *p, Audio *audio, float dt, float world_time) {
    (void)world_time;

    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &em->list[i];
        if (!e->active)
            continue;

        if (e->hurt_timer > 0.0f)
            e->hurt_timer -= dt;

        entity_move(e, w, dt);

        /* Void despawn */
        if (e->pos.y < -10.0f) {
            e->active = false;
            em->count--;
            continue;
        }

        /* Arrow logic */
        if (e->type == ENTITY_ARROW) {
            if (entity_collides(w, e->pos, e->width, e->height)) {
                audio_play_bow_hit(audio);
                e->active = false;
                em->count--;
                continue;
            }
            /* Check collision with player */
            float p_dx = p->pos.x - e->pos.x;
            float p_dy = (p->pos.y + 0.9f) - e->pos.y;
            float p_dz = p->pos.z - e->pos.z;
            float p_dist = sqrtf(p_dx * p_dx + p_dy * p_dy + p_dz * p_dz);
            if (p_dist < 0.75f) {
                player_damage(p, 4.0f);
                audio_play_bow_hit(audio);
                e->active = false;
                em->count--;
                continue;
            }
        }
    }
}

Entity *entity_raycast(EntityManager *em, Vec3 origin, Vec3 dir, float reach) {
    (void)em; (void)origin; (void)dir; (void)reach;
    return NULL;
}

bool entity_damage(EntityManager *em, Entity *e, float damage, Vec3 knockback, Audio *audio) {
    if (!e || !e->active)
        return false;

    e->health -= damage;
    e->hurt_timer = 0.35f;
    e->vel = vec3_add(e->vel, knockback);
    audio_play_hurt(audio);

    if (e->health <= 0.0f) {
        e->active = false;
        if (em) em->count--;
        return true;
    }
    return false;
}

#if MCED_USE_GL
static void draw_shaded_box(float min_x, float min_y, float min_z,
                            float max_x, float max_y, float max_z,
                            float r, float g, float b, bool hurt) {
    if (hurt) {
        r = 1.0f; g = 0.22f; b = 0.22f;
    }
    glBegin(GL_QUADS);
    /* Top (+Y) */
    glColor3f(r * 1.0f, g * 1.0f, b * 1.0f);
    glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, max_y, max_z);
    glVertex3f(min_x, max_y, max_z);

    /* Bottom (-Y) */
    glColor3f(r * 0.5f, g * 0.5f, b * 0.5f);
    glVertex3f(min_x, min_y, min_z);
    glVertex3f(min_x, min_y, max_z);
    glVertex3f(max_x, min_y, max_z);
    glVertex3f(max_x, min_y, min_z);

    /* Front (+Z) */
    glColor3f(r * 0.8f, g * 0.8f, b * 0.8f);
    glVertex3f(min_x, min_y, max_z);
    glVertex3f(max_x, min_y, max_z);
    glVertex3f(max_x, max_y, max_z);
    glVertex3f(min_x, max_y, max_z);

    /* Back (-Z) */
    glColor3f(r * 0.8f, g * 0.8f, b * 0.8f);
    glVertex3f(min_x, min_y, min_z);
    glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, min_y, min_z);

    /* Left (-X) */
    glColor3f(r * 0.65f, g * 0.65f, b * 0.65f);
    glVertex3f(min_x, min_y, min_z);
    glVertex3f(min_x, min_y, max_z);
    glVertex3f(min_x, max_y, max_z);
    glVertex3f(min_x, max_y, min_z);

    /* Right (+X) */
    glColor3f(r * 0.65f, g * 0.65f, b * 0.65f);
    glVertex3f(max_x, min_y, min_z);
    glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, max_y, max_z);
    glVertex3f(max_x, min_y, max_z);
    glEnd();
}
#endif

void entity_render_all(const EntityManager *em, const Assets *assets, const Vec3 *player_pos) {
    (void)assets;
    (void)player_pos;

#if MCED_USE_GL
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    for (int i = 0; i < MAX_ENTITIES; i++) {
        const Entity *e = &em->list[i];
        if (!e->active)
            continue;

        glPushMatrix();
        glTranslatef(e->pos.x, e->pos.y, e->pos.z);
        glRotatef(e->yaw, 0, 1, 0);

        if (e->type == ENTITY_ARROW) {
            float spd = sqrtf(e->vel.x * e->vel.x + e->vel.y * e->vel.y + e->vel.z * e->vel.z);
            if (spd > 0.01f) {
                float pitch = asinf(e->vel.y / spd) * 180.0f / 3.14159f;
                glRotatef(-pitch, 1, 0, 0);
            }
            /* Shaft */
            draw_shaded_box(-0.02f, -0.02f, -0.30f, 0.02f, 0.02f, 0.30f, 0.65f, 0.50f, 0.30f, false);
            /* Fletching */
            draw_shaded_box(-0.06f, -0.06f, -0.30f, 0.06f, 0.06f, -0.20f, 0.90f, 0.90f, 0.90f, false);
            /* Tip */
            draw_shaded_box(-0.04f, -0.04f,  0.25f, 0.04f, 0.04f,  0.35f, 0.35f, 0.35f, 0.35f, false);
        }

        glPopMatrix();
    }
#endif
}

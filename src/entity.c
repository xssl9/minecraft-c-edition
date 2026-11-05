#include "entity.h"
#include "blocks.h"
#include "render.h"

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

    /* Living mobs disabled per user request to eliminate lag and glitches */
    if (type != ENTITY_ARROW)
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
    } else if (type == ENTITY_ZOMBIE) {
        e->width = 0.6f;
        e->height = 1.95f;
        e->health = 20.0f;
        e->max_health = 20.0f;
    } else if (type == ENTITY_SKELETON) {
        e->width = 0.6f;
        e->height = 1.98f;
        e->health = 20.0f;
        e->max_health = 20.0f;
        e->shoot_timer = 1.5f;
    } else if (type == ENTITY_CREEPER) {
        e->width = 0.6f;
        e->height = 1.70f;
        e->health = 20.0f;
        e->max_health = 20.0f;
    } else if (type == ENTITY_PIG) {
        e->width = 0.9f;
        e->height = 0.9f;
        e->health = 10.0f;
        e->max_health = 10.0f;
        e->wander_timer = 2.0f;
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

    /* Gravity */
    e->vel.y -= 22.0f * dt;
    if (e->vel.y < -30.0f) e->vel.y = -30.0f;

    /* Horizontal X */
    Vec3 nx = e->pos;
    nx.x += e->vel.x * dt;
    if (!entity_collides(w, nx, e->width, e->height)) {
        e->pos.x = nx.x;
    } else {
        /* Step up 1 block if on ground */
        Vec3 step_x = nx;
        step_x.y += 1.05f;
        if (e->on_ground && !entity_collides(w, step_x, e->width, e->height)) {
            e->pos.x = nx.x;
            e->pos.y += 1.05f;
        } else {
            e->vel.x = 0;
        }
    }

    /* Horizontal Z */
    Vec3 nz = e->pos;
    nz.z += e->vel.z * dt;
    if (!entity_collides(w, nz, e->width, e->height)) {
        e->pos.z = nz.z;
    } else {
        Vec3 step_z = nz;
        step_z.y += 1.05f;
        if (e->on_ground && !entity_collides(w, step_z, e->width, e->height)) {
            e->pos.z = nz.z;
            e->pos.y += 1.05f;
        } else {
            e->vel.z = 0;
        }
    }

    /* Vertical Y */
    Vec3 ny = e->pos;
    ny.y += e->vel.y * dt;
    if (!entity_collides(w, ny, e->width, e->height)) {
        e->pos.y = ny.y;
        e->on_ground = false;
    } else {
        if (e->vel.y < 0.0f) {
            e->on_ground = true;
            e->pos.y = ceilf(e->pos.y);
            while (entity_collides(w, e->pos, e->width, e->height))
                e->pos.y += 0.05f;
        }
        e->vel.y = 0;
    }

    /* Ground friction / air resistance */
    float drag = e->on_ground ? 0.65f : 0.95f;
    e->vel.x *= powf(drag, dt * 20.0f);
    e->vel.z *= powf(drag, dt * 20.0f);

    /* Walk animation pos & speed */
    float h_speed = sqrtf(e->vel.x * e->vel.x + e->vel.z * e->vel.z);
    e->walk_anim_speed = fminf(1.0f, h_speed * 0.4f);
    e->walk_anim_pos += h_speed * dt * 4.0f;
}

static void explode_creeper(World *w, EntityManager *em, Player *p, Audio *audio, float cx, float cy, float cz) {
    (void)em;
    audio_play_explode(audio);

    /* Damage player */
    float pdx = p->pos.x - cx;
    float pdy = (p->pos.y + 0.9f) - cy;
    float pdz = p->pos.z - cz;
    float pdist = sqrtf(pdx * pdx + pdy * pdy + pdz * pdz);
    if (pdist < 6.0f) {
        float dmg = (1.0f - pdist / 6.0f) * 18.0f;
        player_damage(p, dmg);
    }

    /* Crater destruction */
    int r = 3;
    int icx = (int)floorf(cx);
    int icy = (int)floorf(cy);
    int icz = (int)floorf(cz);
    for (int dy = -r; dy <= r; dy++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy + dz * dz <= r * r) {
                    int bx = icx + dx, by = icy + dy, bz = icz + dz;
                    uint8_t b = world_get_block(w, bx, by, bz);
                    if (b != BLOCK_AIR && b != BLOCK_BEDROCK && b != BLOCK_OBSIDIAN) {
                        world_set_block(w, bx, by, bz, BLOCK_AIR, true);
                    }
                }
            }
        }
    }
}

void entity_update_all(EntityManager *em, World *w, Player *p, Audio *audio, float dt, float world_time) {
    (void)world_time;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &em->list[i];
        if (!e->active)
            continue;

        /* Living mobs disabled per user request */
        if (e->type != ENTITY_ARROW) {
            e->active = false;
            em->count--;
            continue;
        }

        if (e->hurt_timer > 0.0f)
            e->hurt_timer -= dt;
        if (e->attack_cooldown > 0.0f)
            e->attack_cooldown -= dt;

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

        /* Mob behaviors (Zombie, Skeleton, Creeper, Pig) */
        if (e->type == ENTITY_ZOMBIE || e->type == ENTITY_SKELETON ||
            e->type == ENTITY_CREEPER || e->type == ENTITY_PIG) {
            float dx = p->pos.x - e->pos.x;
            float dy = p->pos.y - e->pos.y;
            float dz = p->pos.z - e->pos.z;
            float dist = sqrtf(dx * dx + dz * dz);
            float dist3d = sqrtf(dx * dx + dy * dy + dz * dz);

            /* Pig: Peaceful wandering */
            if (e->type == ENTITY_PIG) {
                e->wander_timer -= dt;
                if (e->wander_timer <= 0.0f) {
                    e->wander_timer = 2.0f + (float)(rand() % 4);
                    e->wander_yaw = (float)(rand() % 360);
                }
                float rad = e->wander_yaw * 3.14159f / 180.0f;
                e->vel.x += sinf(rad) * 1.5f * dt;
                e->vel.z += cosf(rad) * 1.5f * dt;
                e->yaw = e->wander_yaw;
                continue;
            }

            /* Hostile AI: Target player if within 16 blocks */
            if (dist < 16.0f && fabsf(dy) < 8.0f && !p->dead) {
                float target_yaw = atan2f(dx, dz) * 180.0f / 3.14159f;
                e->yaw = target_yaw;
                e->head_yaw = target_yaw;
                e->head_pitch = -asinf(dy / fmaxf(1.0f, dist3d)) * 180.0f / 3.14159f;

                if (e->type == ENTITY_ZOMBIE) {
                    float rad = target_yaw * 3.14159f / 180.0f;
                    float speed = 3.2f;
                    e->vel.x += sinf(rad) * speed * dt;
                    e->vel.z += cosf(rad) * speed * dt;

                    if (dist3d < 1.35f && e->attack_cooldown <= 0.0f) {
                        player_damage(p, 3.0f);
                        audio_play_hurt(audio);
                        e->attack_cooldown = 1.0f;
                    }
                } else if (e->type == ENTITY_CREEPER) {
                    float rad = target_yaw * 3.14159f / 180.0f;
                    float speed = 2.8f;
                    e->vel.x += sinf(rad) * speed * dt;
                    e->vel.z += cosf(rad) * speed * dt;

                    if (dist3d < 3.2f) {
                        e->fuse_timer += dt;
                        if (e->fuse_timer >= 1.5f) {
                            e->active = false;
                            em->count--;
                            explode_creeper(w, em, p, audio, e->pos.x, e->pos.y, e->pos.z);
                            continue;
                        }
                    } else if (e->fuse_timer > 0.0f) {
                        e->fuse_timer -= dt * 0.5f;
                    }
                } else if (e->type == ENTITY_SKELETON) {
                    float rad = target_yaw * 3.14159f / 180.0f;
                    if (dist < 7.0f) {
                        e->vel.x -= sinf(rad) * 2.5f * dt;
                        e->vel.z -= cosf(rad) * 2.5f * dt;
                    } else if (dist > 11.0f) {
                        e->vel.x += sinf(rad) * 2.5f * dt;
                        e->vel.z += cosf(rad) * 2.5f * dt;
                    }
                    e->shoot_timer -= dt;
                    if (e->shoot_timer <= 0.0f) {
                        e->shoot_timer = 2.0f + (float)(rand() % 10) * 0.1f;
                        Vec3 arrow_origin = vec3(e->pos.x, e->pos.y + 1.4f, e->pos.z);
                        Entity *arr = entity_spawn(em, ENTITY_ARROW, arrow_origin);
                        if (arr) {
                            Vec3 to_p = vec3((p->pos.x - arrow_origin.x) / dist3d,
                                             (p->pos.y + 0.8f - arrow_origin.y) / dist3d,
                                             (p->pos.z - arrow_origin.z) / dist3d);
                            arr->vel = vec3_mul(to_p, 18.0f);
                            arr->yaw = target_yaw;
                            audio_play_bow(audio);
                        }
                    }
                }
            } else {
                /* Wander around when idle */
                e->wander_timer -= dt;
                if (e->wander_timer <= 0.0f) {
                    e->wander_timer = 3.0f + (float)(rand() % 4);
                    e->wander_yaw = (float)(rand() % 360);
                }
                float rad = e->wander_yaw * 3.14159f / 180.0f;
                e->vel.x += sinf(rad) * 1.2f * dt;
                e->vel.z += cosf(rad) * 1.2f * dt;
                e->yaw = e->wander_yaw;
            }

            /* Daylight burning for undead */
            if ((e->type == ENTITY_ZOMBIE || e->type == ENTITY_SKELETON) && world_time > 0.22f && world_time < 0.78f) {
                int top_y = world_height_at(w, (int)floorf(e->pos.x), (int)floorf(e->pos.z));
                if (e->pos.y >= (float)top_y - 0.5f) {
                    entity_damage(em, e, 1.0f * dt, vec3(0, 0, 0), audio);
                }
            }
        }
    }
}

Entity *entity_raycast(EntityManager *em, Vec3 origin, Vec3 dir, float reach) {
    Entity *closest = NULL;
    float closest_dist = reach;

    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &em->list[i];
        if (!e->active || e->type == ENTITY_ARROW) continue;

        float hw = e->width * 0.5f;
        float min_x = e->pos.x - hw, max_x = e->pos.x + hw;
        float min_y = e->pos.y,      max_y = e->pos.y + e->height;
        float min_z = e->pos.z - hw, max_z = e->pos.z + hw;

        float t1 = (min_x - origin.x) / (dir.x != 0 ? dir.x : 1e-6f);
        float t2 = (max_x - origin.x) / (dir.x != 0 ? dir.x : 1e-6f);
        float t3 = (min_y - origin.y) / (dir.y != 0 ? dir.y : 1e-6f);
        float t4 = (max_y - origin.y) / (dir.y != 0 ? dir.y : 1e-6f);
        float t5 = (min_z - origin.z) / (dir.z != 0 ? dir.z : 1e-6f);
        float t6 = (max_z - origin.z) / (dir.z != 0 ? dir.z : 1e-6f);

        float tmin = fmaxf(fmaxf(fminf(t1, t2), fminf(t3, t4)), fminf(t5, t6));
        float tmax = fminf(fminf(fmaxf(t1, t2), fmaxf(t3, t4)), fmaxf(t5, t6));

        if (tmax >= 0 && tmin <= tmax && tmin < closest_dist && tmin >= 0.0f) {
            closest_dist = tmin;
            closest = e;
        }
    }
    return closest;
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

static void draw_box_uv(float min_x, float min_y, float min_z,
                        float max_x, float max_y, float max_z,
                        int u0, int v0, int dx, int dy, int dz,
                        float tw, float th,
                        bool hurt, float flash_r, float flash_g, float flash_b) {
    float inv_w = 1.0f / tw;
    float inv_h = 1.0f / th;

    float u_top0 = (float)(u0 + dz) * inv_w,       v_top0 = (float)v0 * inv_h;
    float u_top1 = (float)(u0 + dz + dx) * inv_w,  v_top1 = (float)(v0 + dz) * inv_h;

    float u_bot0 = (float)(u0 + dz + dx) * inv_w,  v_bot0 = (float)v0 * inv_h;
    float u_bot1 = (float)(u0 + dz + 2 * dx) * inv_w, v_bot1 = (float)(v0 + dz) * inv_h;

    float u_rt0  = (float)u0 * inv_w,              v_rt0  = (float)(v0 + dz) * inv_h;
    float u_rt1  = (float)(u0 + dz) * inv_w,       v_rt1  = (float)(v0 + dz + dy) * inv_h;

    float u_fr0  = (float)(u0 + dz) * inv_w,       v_fr0  = (float)(v0 + dz) * inv_h;
    float u_fr1  = (float)(u0 + dz + dx) * inv_w,  v_fr1  = (float)(v0 + dz + dy) * inv_h;

    float u_lt0  = (float)(u0 + dz + dx) * inv_w,  v_lt0  = (float)(v0 + dz) * inv_h;
    float u_lt1  = (float)(u0 + 2 * dz + dx) * inv_w, v_lt1  = (float)(v0 + dz + dy) * inv_h;

    float u_bk0  = (float)(u0 + 2 * dz + dx) * inv_w, v_bk0  = (float)(v0 + dz) * inv_h;
    float u_bk1  = (float)(u0 + 2 * dz + 2 * dx) * inv_w, v_bk1 = (float)(v0 + dz + dy) * inv_h;

    float r = flash_r, g = flash_g, b = flash_b;
    if (hurt) {
        r = 1.0f; g = 0.30f; b = 0.30f;
    }

    glBegin(GL_QUADS);
    /* Top (+Y) */
    glColor3f(r * 1.0f, g * 1.0f, b * 1.0f);
    glTexCoord2f(u_top0, v_top0); glVertex3f(min_x, max_y, min_z);
    glTexCoord2f(u_top0, v_top1); glVertex3f(min_x, max_y, max_z);
    glTexCoord2f(u_top1, v_top1); glVertex3f(max_x, max_y, max_z);
    glTexCoord2f(u_top1, v_top0); glVertex3f(max_x, max_y, min_z);

    /* Bottom (-Y) */
    glColor3f(r * 0.55f, g * 0.55f, b * 0.55f);
    glTexCoord2f(u_bot0, v_bot0); glVertex3f(min_x, min_y, min_z);
    glTexCoord2f(u_bot0, v_bot1); glVertex3f(min_x, min_y, max_z);
    glTexCoord2f(u_bot1, v_bot1); glVertex3f(max_x, min_y, max_z);
    glTexCoord2f(u_bot1, v_bot0); glVertex3f(max_x, min_y, min_z);

    /* Front (+Z) */
    glColor3f(r * 0.85f, g * 0.85f, b * 0.85f);
    glTexCoord2f(u_fr0, v_fr0); glVertex3f(min_x, min_y, max_z);
    glTexCoord2f(u_fr1, v_fr0); glVertex3f(max_x, min_y, max_z);
    glTexCoord2f(u_fr1, v_fr1); glVertex3f(max_x, max_y, max_z);
    glTexCoord2f(u_fr0, v_fr1); glVertex3f(min_x, max_y, max_z);

    /* Back (-Z) */
    glColor3f(r * 0.80f, g * 0.80f, b * 0.80f);
    glTexCoord2f(u_bk0, v_bk0); glVertex3f(min_x, min_y, min_z);
    glTexCoord2f(u_bk1, v_bk0); glVertex3f(max_x, min_y, min_z);
    glTexCoord2f(u_bk1, v_bk1); glVertex3f(max_x, max_y, min_z);
    glTexCoord2f(u_bk0, v_bk1); glVertex3f(min_x, max_y, min_z);

    /* Left (-X) */
    glColor3f(r * 0.70f, g * 0.70f, b * 0.70f);
    glTexCoord2f(u_lt0, v_lt0); glVertex3f(min_x, min_y, min_z);
    glTexCoord2f(u_lt0, v_lt1); glVertex3f(min_x, min_y, max_z);
    glTexCoord2f(u_lt1, v_lt1); glVertex3f(min_x, max_y, max_z);
    glTexCoord2f(u_lt1, v_lt0); glVertex3f(min_x, max_y, min_z);

    /* Right (+X) */
    glColor3f(r * 0.70f, g * 0.70f, b * 0.70f);
    glTexCoord2f(u_rt0, v_rt0); glVertex3f(max_x, min_y, min_z);
    glTexCoord2f(u_rt0, v_rt1); glVertex3f(max_x, min_y, max_z);
    glTexCoord2f(u_rt1, v_rt1); glVertex3f(max_x, max_y, max_z);
    glTexCoord2f(u_rt1, v_rt0); glVertex3f(max_x, max_y, min_z);
    glEnd();
}
#endif

void entity_render_all(const EntityManager *em, const Renderer *renderer, const Vec3 *player_pos) {
    (void)player_pos;
    const Renderer *r = renderer;

#if MCED_USE_GL
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    for (int i = 0; i < MAX_ENTITIES; i++) {
        const Entity *e = &em->list[i];
        if (!e->active || e->type != ENTITY_ARROW)
            continue;

        glPushMatrix();
        glTranslatef(e->pos.x, e->pos.y, e->pos.z);
        glRotatef(e->yaw, 0, 1, 0);

        bool hurt = e->hurt_timer > 0.0f;

        if (e->type == ENTITY_ARROW) {
            glDisable(GL_TEXTURE_2D);
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
        } else if (e->type == ENTITY_ZOMBIE) {
            float leg_swing = cosf(e->walk_anim_pos * 0.6662f) * 1.4f * e->walk_anim_speed * 180.0f / 3.14159f;

            if (r && r->mob_zombie_tex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, r->mob_zombie_tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                /* Head */
                glPushMatrix();
                glTranslatef(0.0f, 1.45f, 0.0f);
                glRotatef(e->head_pitch, 1, 0, 0);
                draw_box_uv(-0.25f, 0.0f, -0.25f, 0.25f, 0.50f, 0.25f, 0, 0, 8, 8, 8, 64, 64, hurt, 1, 1, 1);
                glPopMatrix();

                /* Torso */
                draw_box_uv(-0.25f, 0.75f, -0.15f, 0.25f, 1.45f, 0.15f, 16, 16, 8, 12, 4, 64, 64, hurt, 1, 1, 1);

                /* Left Leg */
                glPushMatrix();
                glTranslatef(-0.13f, 0.75f, 0.0f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_box_uv(-0.11f, -0.75f, -0.11f, 0.11f, 0.0f, 0.11f, 0, 16, 4, 12, 4, 64, 64, hurt, 1, 1, 1);
                glPopMatrix();

                /* Right Leg */
                glPushMatrix();
                glTranslatef(0.13f, 0.75f, 0.0f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_box_uv(-0.11f, -0.75f, -0.11f, 0.11f, 0.0f, 0.11f, 0, 16, 4, 12, 4, 64, 64, hurt, 1, 1, 1);
                glPopMatrix();

                /* Left Arm (raised forward) */
                glPushMatrix();
                glTranslatef(-0.35f, 1.40f, 0.0f);
                glRotatef(-78.0f + sinf(e->walk_anim_pos * 0.333f) * 6.0f, 1, 0, 0);
                draw_box_uv(-0.10f, -0.70f, -0.10f, 0.10f, 0.0f, 0.10f, 40, 16, 4, 12, 4, 64, 64, hurt, 1, 1, 1);
                glPopMatrix();

                /* Right Arm (raised forward) */
                glPushMatrix();
                glTranslatef(0.35f, 1.40f, 0.0f);
                glRotatef(-78.0f - sinf(e->walk_anim_pos * 0.333f) * 6.0f, 1, 0, 0);
                draw_box_uv(-0.10f, -0.70f, -0.10f, 0.10f, 0.0f, 0.10f, 40, 16, 4, 12, 4, 64, 64, hurt, 1, 1, 1);
                glPopMatrix();
            } else {
                glDisable(GL_TEXTURE_2D);
                glPushMatrix();
                glTranslatef(0.0f, 1.45f, 0.0f);
                glRotatef(e->head_pitch, 1, 0, 0);
                draw_shaded_box(-0.25f, 0.0f, -0.25f, 0.25f, 0.50f, 0.25f, 0.18f, 0.45f, 0.18f, hurt);
                glPopMatrix();
                draw_shaded_box(-0.25f, 0.75f, -0.15f, 0.25f, 1.45f, 0.15f, 0.15f, 0.48f, 0.55f, hurt);
                glPushMatrix();
                glTranslatef(-0.13f, 0.75f, 0.0f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_shaded_box(-0.11f, -0.75f, -0.11f, 0.11f, 0.0f, 0.11f, 0.15f, 0.18f, 0.48f, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.13f, 0.75f, 0.0f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_shaded_box(-0.11f, -0.75f, -0.11f, 0.11f, 0.0f, 0.11f, 0.15f, 0.18f, 0.48f, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(-0.35f, 1.40f, 0.0f);
                glRotatef(-78.0f + sinf(e->walk_anim_pos * 0.333f) * 6.0f, 1, 0, 0);
                draw_shaded_box(-0.10f, -0.70f, -0.10f, 0.10f, 0.0f, 0.10f, 0.18f, 0.45f, 0.18f, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.35f, 1.40f, 0.0f);
                glRotatef(-78.0f - sinf(e->walk_anim_pos * 0.333f) * 6.0f, 1, 0, 0);
                draw_shaded_box(-0.10f, -0.70f, -0.10f, 0.10f, 0.0f, 0.10f, 0.18f, 0.45f, 0.18f, hurt);
                glPopMatrix();
            }
        } else if (e->type == ENTITY_SKELETON) {
            float leg_swing = cosf(e->walk_anim_pos * 0.6662f) * 1.4f * e->walk_anim_speed * 180.0f / 3.14159f;

            if (r && r->mob_skeleton_tex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, r->mob_skeleton_tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                /* Head (Skull) */
                glPushMatrix();
                glTranslatef(0.0f, 1.45f, 0.0f);
                glRotatef(e->head_pitch, 1, 0, 0);
                draw_box_uv(-0.25f, 0.0f, -0.25f, 0.25f, 0.50f, 0.25f, 0, 0, 8, 8, 8, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                /* Torso (Ribcage) */
                draw_box_uv(-0.22f, 0.75f, -0.12f, 0.22f, 1.45f, 0.12f, 16, 16, 8, 12, 4, 64, 32, hurt, 1, 1, 1);

                /* Left Leg */
                glPushMatrix();
                glTranslatef(-0.12f, 0.75f, 0.0f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_box_uv(-0.06f, -0.75f, -0.06f, 0.06f, 0.0f, 0.06f, 0, 16, 2, 12, 2, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                /* Right Leg */
                glPushMatrix();
                glTranslatef(0.12f, 0.75f, 0.0f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_box_uv(-0.06f, -0.75f, -0.06f, 0.06f, 0.0f, 0.06f, 0, 16, 2, 12, 2, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                /* Left Arm (aiming bow) */
                glPushMatrix();
                glTranslatef(-0.30f, 1.40f, 0.0f);
                glRotatef(-85.0f, 1, 0, 0);
                glRotatef(-15.0f, 0, 1, 0);
                draw_box_uv(-0.06f, -0.70f, -0.06f, 0.06f, 0.0f, 0.06f, 40, 16, 2, 12, 2, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                /* Right Arm (aiming bow) */
                glPushMatrix();
                glTranslatef(0.30f, 1.40f, 0.0f);
                glRotatef(-85.0f, 1, 0, 0);
                glRotatef(15.0f, 0, 1, 0);
                draw_box_uv(-0.06f, -0.70f, -0.06f, 0.06f, 0.0f, 0.06f, 40, 16, 2, 12, 2, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();
            } else {
                glDisable(GL_TEXTURE_2D);
                glPushMatrix();
                glTranslatef(0.0f, 1.45f, 0.0f);
                glRotatef(e->head_pitch, 1, 0, 0);
                draw_shaded_box(-0.25f, 0.0f, -0.25f, 0.25f, 0.50f, 0.25f, 0.85f, 0.85f, 0.82f, hurt);
                glPopMatrix();
                draw_shaded_box(-0.22f, 0.75f, -0.12f, 0.22f, 1.45f, 0.12f, 0.80f, 0.80f, 0.78f, hurt);
                glPushMatrix();
                glTranslatef(-0.12f, 0.75f, 0.0f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_shaded_box(-0.06f, -0.75f, -0.06f, 0.06f, 0.0f, 0.06f, 0.80f, 0.80f, 0.78f, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.12f, 0.75f, 0.0f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_shaded_box(-0.06f, -0.75f, -0.06f, 0.06f, 0.0f, 0.06f, 0.80f, 0.80f, 0.78f, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(-0.30f, 1.40f, 0.0f);
                glRotatef(-85.0f, 1, 0, 0);
                glRotatef(-15.0f, 0, 1, 0);
                draw_shaded_box(-0.06f, -0.70f, -0.06f, 0.06f, 0.0f, 0.06f, 0.80f, 0.80f, 0.78f, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.30f, 1.40f, 0.0f);
                glRotatef(-85.0f, 1, 0, 0);
                glRotatef(15.0f, 0, 1, 0);
                draw_shaded_box(-0.06f, -0.70f, -0.06f, 0.06f, 0.0f, 0.06f, 0.80f, 0.80f, 0.78f, hurt);
                glPopMatrix();
            }
        } else if (e->type == ENTITY_CREEPER) {
            float leg_swing = cosf(e->walk_anim_pos * 0.6662f) * 1.4f * e->walk_anim_speed * 180.0f / 3.14159f;

            /* Fuse flash and swelling */
            float swell = 1.0f + e->fuse_timer * 0.15f;
            glScalef(swell, swell, swell);
            bool fuse_flash = (e->fuse_timer > 0.0f && ((int)(e->fuse_timer * 8.0f) & 1));
            float cr = fuse_flash ? 1.5f : 1.0f;
            float cg = fuse_flash ? 1.5f : 1.0f;
            float cb = fuse_flash ? 1.5f : 1.0f;

            if (r && r->mob_creeper_tex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, r->mob_creeper_tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                /* Head */
                glPushMatrix();
                glTranslatef(0.0f, 1.15f, 0.0f);
                glRotatef(e->head_pitch, 1, 0, 0);
                draw_box_uv(-0.25f, 0.0f, -0.25f, 0.25f, 0.50f, 0.25f, 0, 0, 8, 8, 8, 64, 32, hurt, cr, cg, cb);
                glPopMatrix();

                /* Body */
                draw_box_uv(-0.22f, 0.35f, -0.15f, 0.22f, 1.15f, 0.15f, 16, 16, 8, 12, 4, 64, 32, hurt, cr, cg, cb);

                /* 4 Legs */
                glPushMatrix();
                glTranslatef(-0.13f, 0.35f, 0.18f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_box_uv(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0, 16, 4, 6, 4, 64, 32, hurt, cr, cg, cb);
                glPopMatrix();

                glPushMatrix();
                glTranslatef(0.13f, 0.35f, 0.18f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_box_uv(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0, 16, 4, 6, 4, 64, 32, hurt, cr, cg, cb);
                glPopMatrix();

                glPushMatrix();
                glTranslatef(-0.13f, 0.35f, -0.18f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_box_uv(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0, 16, 4, 6, 4, 64, 32, hurt, cr, cg, cb);
                glPopMatrix();

                glPushMatrix();
                glTranslatef(0.13f, 0.35f, -0.18f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_box_uv(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0, 16, 4, 6, 4, 64, 32, hurt, cr, cg, cb);
                glPopMatrix();
            } else {
                glDisable(GL_TEXTURE_2D);
                glPushMatrix();
                glTranslatef(0.0f, 1.15f, 0.0f);
                glRotatef(e->head_pitch, 1, 0, 0);
                draw_shaded_box(-0.25f, 0.0f, -0.25f, 0.25f, 0.50f, 0.25f, 0.18f * cr, 0.62f * cg, 0.18f * cb, hurt);
                glPopMatrix();
                draw_shaded_box(-0.22f, 0.35f, -0.15f, 0.22f, 1.15f, 0.15f, 0.18f * cr, 0.62f * cg, 0.18f * cb, hurt);
                glPushMatrix();
                glTranslatef(-0.13f, 0.35f, 0.18f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_shaded_box(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0.18f * cr, 0.62f * cg, 0.18f * cb, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.13f, 0.35f, 0.18f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_shaded_box(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0.18f * cr, 0.62f * cg, 0.18f * cb, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(-0.13f, 0.35f, -0.18f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_shaded_box(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0.18f * cr, 0.62f * cg, 0.18f * cb, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.13f, 0.35f, -0.18f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_shaded_box(-0.10f, -0.35f, -0.10f, 0.10f, 0.0f, 0.10f, 0.18f * cr, 0.62f * cg, 0.18f * cb, hurt);
                glPopMatrix();
            }
        } else if (e->type == ENTITY_PIG) {
            float leg_swing = cosf(e->walk_anim_pos * 0.6662f) * 1.4f * e->walk_anim_speed * 180.0f / 3.14159f;

            if (r && r->mob_pig_tex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, r->mob_pig_tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                /* Horizontal Body */
                draw_box_uv(-0.30f, 0.25f, -0.45f, 0.30f, 0.65f, 0.45f, 28, 8, 10, 16, 8, 64, 32, hurt, 1, 1, 1);

                /* Head & Snout */
                glPushMatrix();
                glTranslatef(0.0f, 0.45f, 0.45f);
                draw_box_uv(-0.20f, 0.0f, 0.0f, 0.20f, 0.40f, 0.30f, 0, 0, 8, 8, 8, 64, 32, hurt, 1, 1, 1);
                /* Snout */
                draw_box_uv(-0.10f, 0.05f, 0.30f, 0.10f, 0.20f, 0.38f, 16, 16, 4, 3, 1, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                /* 4 Legs */
                glPushMatrix();
                glTranslatef(-0.18f, 0.25f, 0.25f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_box_uv(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, 0, 16, 4, 6, 4, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                glPushMatrix();
                glTranslatef(0.18f, 0.25f, 0.25f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_box_uv(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, 0, 16, 4, 6, 4, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                glPushMatrix();
                glTranslatef(-0.18f, 0.25f, -0.25f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_box_uv(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, 0, 16, 4, 6, 4, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();

                glPushMatrix();
                glTranslatef(0.18f, 0.25f, -0.25f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_box_uv(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, 0, 16, 4, 6, 4, 64, 32, hurt, 1, 1, 1);
                glPopMatrix();
            } else {
                float pr = 0.92f, pg = 0.62f, pb = 0.62f;
                glDisable(GL_TEXTURE_2D);
                draw_shaded_box(-0.30f, 0.25f, -0.45f, 0.30f, 0.65f, 0.45f, pr, pg, pb, hurt);
                glPushMatrix();
                glTranslatef(0.0f, 0.45f, 0.45f);
                draw_shaded_box(-0.20f, 0.0f, 0.0f, 0.20f, 0.40f, 0.30f, pr, pg, pb, hurt);
                draw_shaded_box(-0.10f, 0.05f, 0.30f, 0.10f, 0.20f, 0.38f, pr * 0.95f, pg * 0.85f, pb * 0.85f, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(-0.18f, 0.25f, 0.25f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_shaded_box(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, pr, pg, pb, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.18f, 0.25f, 0.25f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_shaded_box(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, pr, pg, pb, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(-0.18f, 0.25f, -0.25f);
                glRotatef(-leg_swing, 1, 0, 0);
                draw_shaded_box(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, pr, pg, pb, hurt);
                glPopMatrix();
                glPushMatrix();
                glTranslatef(0.18f, 0.25f, -0.25f);
                glRotatef(leg_swing, 1, 0, 0);
                draw_shaded_box(-0.08f, -0.25f, -0.08f, 0.08f, 0.0f, 0.08f, pr, pg, pb, hurt);
                glPopMatrix();
            }
        }

        glPopMatrix();
    }
    glDisable(GL_TEXTURE_2D);
#endif
}

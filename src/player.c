#include "player.h"

#include "blocks.h"

#include <math.h>

Vec3 player_eye(const Player *p) {
    return vec3(p->pos.x, p->pos.y + PLAYER_EYE, p->pos.z);
}

Vec3 player_forward(const Player *p) {
    float yaw = p->yaw * MCED_DEG2RAD;
    float pitch = p->pitch * MCED_DEG2RAD;
    float cp = cosf(pitch);
    return vec3(-sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp);
}

void player_init(Player *p, Vec3 spawn) {
    memset(p, 0, sizeof(*p));
    p->pos = spawn;
    p->yaw = 0;
    p->pitch = 0;
    p->on_ground = false;
    p->health = PLAYER_MAX_HEALTH;
    p->air = PLAYER_MAX_AIR;
    p->hunger = PLAYER_MAX_HUNGER;
    p->saturation = 5.0f;
    p->exhaustion = 0.0f;
    p->armor_points = 0;
    p->level = 0;
    p->exp_progress = 0.0f;
    p->creative = false;
    p->selected = 0;
    p->hotbar[0] = BLOCK_GRASS_BLOCK; p->hotbar_counts[0] = 64;
    p->hotbar[1] = BLOCK_DIRT;        p->hotbar_counts[1] = 64;
    p->hotbar[2] = BLOCK_STONE;       p->hotbar_counts[2] = 64;
    p->hotbar[3] = BLOCK_OAK_PLANKS;  p->hotbar_counts[3] = 64;
    p->hotbar[4] = BLOCK_GLASS;       p->hotbar_counts[4] = 64;
    p->hotbar[5] = BLOCK_OAK_LOG;     p->hotbar_counts[5] = 64;
    p->hotbar[6] = BLOCK_SAND;        p->hotbar_counts[6] = 64;
    p->hotbar[7] = BLOCK_GLOWSTONE;   p->hotbar_counts[7] = 64;
    p->hotbar[8] = BLOCK_COBBLESTONE; p->hotbar_counts[8] = 64;
    player_update_armor_points(p);
}

void player_update_armor_points(Player *p) {
    int total = 0;
    for (int i = 0; i < ARMOR_SLOTS; i++) {
        total += armor_points_for_item(p->armor[i]);
    }
    p->armor_points = total;
}

void player_damage(Player *p, float amount) {
    if (p->dead || amount <= 0 || p->creative)
        return;
    float reduction = (float)p->armor_points * 0.04f;
    if (reduction > 0.80f) reduction = 0.80f;
    float actual_damage = amount * (1.0f - reduction);
    if (actual_damage < 1.0f && amount >= 1.0f) actual_damage = 1.0f;
    p->health -= actual_damage;
    p->hurt_timer = 0.4f;
    p->regen_timer = 0;
    if (p->health <= 0) {
        p->health = 0;
        p->dead = true;
        p->vel = vec3(0, 0, 0);
    }
}

void player_respawn(Player *p, Vec3 spawn) {
    p->pos = spawn;
    p->vel = vec3(0, 0, 0);
    p->health = PLAYER_MAX_HEALTH;
    p->air = PLAYER_MAX_AIR;
    p->hunger = PLAYER_MAX_HUNGER;
    p->saturation = 5.0f;
    p->exhaustion = 0.0f;
    p->fall_distance = 0;
    p->hurt_timer = 0;
    p->regen_timer = 0;
    p->dead = false;
    p->on_ground = false;
    p->in_water = false;
    p->in_lava = false;
}

bool player_aabb_intersects_block(const Player *p, int bx, int by, int bz) {
    float hw = PLAYER_WIDTH * 0.5f;
    float minx = p->pos.x - hw, maxx = p->pos.x + hw;
    float miny = p->pos.y, maxy = p->pos.y + PLAYER_HEIGHT;
    float minz = p->pos.z - hw, maxz = p->pos.z + hw;
    return maxx > bx && minx < bx + 1 &&
           maxy > by && miny < by + 1 &&
           maxz > bz && minz < bz + 1;
}

static bool collides(World *w, Vec3 pos) {
    float hw = PLAYER_WIDTH * 0.5f;
    const float e = 0.001f;
    int x0 = (int)floorf(pos.x - hw + e);
    int x1 = (int)floorf(pos.x + hw - e);
    int y0 = (int)floorf(pos.y + e);
    int y1 = (int)floorf(pos.y + PLAYER_HEIGHT - e);
    int z0 = (int)floorf(pos.z - hw + e);
    int z1 = (int)floorf(pos.z + hw - e);
    for (int y = y0; y <= y1; y++) {
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                uint8_t b = world_get_block(w, x, y, z);
                if (b == BLOCK_AIR) continue;
                const BlockDef *def = &g_block_defs[b];
                if (!def->solid) continue;

                if (def->shape == SHAPE_SLAB) {
                    if (pos.y + e < (float)y + 0.5f && pos.y + PLAYER_HEIGHT - e > (float)y)
                        return true;
                } else {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool has_ground_support(World *w, Vec3 pos) {
    Vec3 p = pos;
    p.y -= 0.05f;
    return collides(w, p);
}

/* Move along one axis, stopping at the first collision or ledge if sneaking. */
static void move_axis(World *w, Player *p, int axis, float delta, bool sneak_hold) {
    if (delta == 0)
        return;
    int steps = (int)ceilf(fabsf(delta) / 0.35f);
    if (steps < 1)
        steps = 1;
    float step = delta / (float)steps;
    for (int s = 0; s < steps; s++) {
        Vec3 old = p->pos;
        Vec3 cand = old;
        if (axis == 0) cand.x += step;
        else if (axis == 1) cand.y += step;
        else cand.z += step;

        if (sneak_hold && (axis == 0 || axis == 2)) {
            if (!has_ground_support(w, cand)) {
                if (axis == 0) p->vel.x = 0;
                else p->vel.z = 0;
                break;
            }
        }

        if (!collides(w, cand)) {
            p->pos = cand;
            continue;
        }

        /* Auto-step assist up slabs & steps up to 0.51m */
        if (p->on_ground && (axis == 0 || axis == 2) && !sneak_hold) {
            Vec3 stepped = cand;
            stepped.y += 0.51f;
            if (!collides(w, stepped)) {
                p->pos = stepped;
                continue;
            }
        }

        /* Binary search the contact point for smooth sliding along walls. */
        float lo = 0.0f, hi = step;
        for (int i = 0; i < 6; i++) {
            float mid = (lo + hi) * 0.5f;
            Vec3 test = old;
            if (axis == 0) test.x += mid;
            else if (axis == 1) test.y += mid;
            else test.z += mid;
            if (collides(w, test) || (sneak_hold && (axis == 0 || axis == 2) && !has_ground_support(w, test)))
                hi = mid;
            else
                lo = mid;
        }
        p->pos = old;
        if (axis == 0) {
            p->pos.x += lo;
            p->vel.x = 0;
        } else if (axis == 1) {
            p->pos.y += lo;
            if (step < 0)
                p->on_ground = true;
            p->vel.y = 0;
        } else {
            p->pos.z += lo;
            p->vel.z = 0;
        }
        break;
    }
}

static uint8_t block_at(World *w, float x, float y, float z) {
    return world_get_block(w, (int)floorf(x), (int)floorf(y), (int)floorf(z));
}

void player_update(Player *p, World *w, const PlayerInput *in, float dt) {
    if (dt > 0.05f)
        dt = 0.05f;
    if (dt <= 0)
        return;

    float prev_y = p->pos.y;
    bool was_ground = p->on_ground;
    p->on_ground = false;
    uint8_t body_block = block_at(w, p->pos.x, p->pos.y + 0.4f, p->pos.z);
    uint8_t head_block = block_at(w, p->pos.x, p->pos.y + PLAYER_EYE, p->pos.z);
    p->in_water = (body_block == BLOCK_WATER || block_is_submerged_flora(body_block));
    p->in_lava = body_block == BLOCK_LAVA;
    bool head_water = (head_block == BLOCK_WATER || block_is_submerged_flora(head_block));

    float yaw = p->yaw * MCED_DEG2RAD;
    Vec3 fwd = vec3(-sinf(yaw), 0, -cosf(yaw));
    Vec3 right = vec3(cosf(yaw), 0, -sinf(yaw));
    float f = (in->forward ? 1 : 0) - (in->back ? 1 : 0);
    float r = (in->right ? 1 : 0) - (in->left ? 1 : 0);
    Vec3 wish = vec3(fwd.x * f + right.x * r, 0, fwd.z * f + right.z * r);
    float wl = vec3_len(wish);
    if (wl > 0.001f)
        wish = vec3_mul(wish, 1.0f / wl);

    bool can_sprint = (p->creative || p->flying || p->hunger > 6.0f);
    float speed = 4.3f;
    if (p->flying)
        speed = (in->sprint && can_sprint) ? 14.0f : 9.0f;
    else if (p->in_water || p->in_lava)
        speed = 3.0f;
    else if (in->sneak)
        speed = 1.35f;
    else if (in->sprint && can_sprint)
        speed = 5.8f;

    float control = p->flying ? 12.0f : (was_ground ? 18.0f : 5.0f);
    if (p->in_water || p->in_lava)
        control = 9.0f;
    float blend = 1.0f - expf(-control * dt);
    p->vel.x += (wish.x * speed - p->vel.x) * blend;
    p->vel.z += (wish.z * speed - p->vel.z) * blend;

    bool jumped = false;
    if (p->flying) {
        float vy = 0;
        if (in->jump) vy += speed;
        if (in->sneak) vy -= speed;
        p->vel.y += (vy - p->vel.y) * (1.0f - expf(-14.0f * dt));
    } else if (p->in_water || p->in_lava) {
        p->vel.y -= 5.0f * dt;
        if (p->vel.y < -5.0f) p->vel.y = -5.0f;
        if (in->jump) {
            p->vel.y = 3.6f;
            jumped = true;
        }
    } else {
        if (in->jump && was_ground) {
            p->vel.y = 8.4f;
            p->on_ground = false;
            jumped = true;
        }
        p->vel.y -= 26.0f * dt;
        if (p->vel.y < -45.0f)
            p->vel.y = -45.0f;
    }

    bool sneak_hold = in->sneak && was_ground && !p->flying;
    move_axis(w, p, 0, p->vel.x * dt, sneak_hold);
    move_axis(w, p, 2, p->vel.z * dt, sneak_hold);
    move_axis(w, p, 1, p->vel.y * dt, false);

    /* Ground probe: catches the frame where gravity has not yet moved us. */
    if (!p->on_ground && p->vel.y <= 0) {
        Vec3 probe = p->pos;
        probe.y -= 0.04f;
        if (collides(w, probe))
            p->on_ground = true;
    }

    /* Survival: fall damage, lava, drowning, and slow regeneration. */
    if (p->creative) {
        p->fall_distance = 0;
        p->air = PLAYER_MAX_AIR;
        p->health = PLAYER_MAX_HEALTH;
        p->hurt_timer = 0;
    } else if (!p->flying) {
        if (p->vel.y < 0 && !p->in_water && !p->in_lava) {
            float dropped = prev_y - p->pos.y;
            if (dropped > 0)
                p->fall_distance += dropped;
        }
        if (p->on_ground) {
            if (!was_ground && p->fall_distance > 3.0f) {
                float dmg = floorf(p->fall_distance - 3.0f);
                if (dmg < 1)
                    dmg = 1;
                player_damage(p, dmg);
            }
            p->fall_distance = 0;
        } else if (p->in_water || p->in_lava) {
            p->fall_distance = 0;
        }

        if (p->in_lava)
            player_damage(p, 4.0f * dt);

        if (head_water) {
            p->air -= dt;
            if (p->air <= 0) {
                p->air = 0;
                player_damage(p, 2.0f * dt);
            }
        } else {
            p->air = PLAYER_MAX_AIR;
            int hx = (int)floorf(p->pos.x);
            int hy = (int)floorf(p->pos.y + PLAYER_EYE);
            int hz = (int)floorf(p->pos.z);
            uint8_t hb = world_get_block(w, hx, hy, hz);
            if (block_is_solid(hb) && block_is_opaque(hb)) {
                player_damage(p, 1.0f * dt);
            }
        }

        /* Hunger exhaustion & sprint drain */
        float horiz_move = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
        if (in->sprint && horiz_move > 0.5f) {
            p->exhaustion += dt * 0.12f;
        } else if (horiz_move > 0.5f) {
            p->exhaustion += dt * 0.02f;
        }
        if (jumped) {
            p->exhaustion += (in->sprint ? 0.15f : 0.05f);
        }
        if (p->exhaustion >= 4.0f) {
            p->exhaustion -= 4.0f;
            if (p->saturation > 0.0f) {
                p->saturation -= 1.0f;
                if (p->saturation < 0.0f) p->saturation = 0.0f;
            } else if (p->hunger > 0.0f) {
                p->hunger -= 1.0f;
                if (p->hunger < 0.0f) p->hunger = 0.0f;
            }
        }

        /* Natural Regeneration from hunger */
        if (!p->dead && p->health < PLAYER_MAX_HEALTH && p->hunger >= 18.0f) {
            p->regen_timer += dt;
            if (p->regen_timer >= 4.0f) {
                p->regen_timer = 0.0f;
                p->health = fminf(PLAYER_MAX_HEALTH, p->health + 1.0f);
                if (p->saturation > 0.0f) {
                    p->saturation -= 1.5f;
                    if (p->saturation < 0.0f) p->saturation = 0.0f;
                } else {
                    p->hunger -= 1.0f;
                    if (p->hunger < 0.0f) p->hunger = 0.0f;
                }
            }
        } else if (p->hunger <= 0.0f) {
            p->regen_timer += dt;
            if (p->regen_timer >= 4.0f) {
                p->regen_timer = 0.0f;
                if (p->health > 1.0f) player_damage(p, 1.0f);
            }
        } else {
            p->regen_timer = 0.0f;
        }
    } else {
        p->fall_distance = 0;
        p->air = PLAYER_MAX_AIR;
        p->hunger = PLAYER_MAX_HUNGER;
        p->saturation = 5.0f;
    }

    if (p->hurt_timer > 0) {
        p->hurt_timer -= dt;
        if (p->hurt_timer < 0)
            p->hurt_timer = 0;
    }
    if (p->swing_timer > 0) {
        p->swing_timer -= dt;
        if (p->swing_timer < 0)
            p->swing_timer = 0;
    }
    if (!p->dead && p->health < PLAYER_MAX_HEALTH && !p->in_lava) {
        p->regen_timer += dt;
        if (p->regen_timer > 7.0f)
            p->health = fminf(PLAYER_MAX_HEALTH, p->health + dt);
    } else if (p->health >= PLAYER_MAX_HEALTH) {
        p->regen_timer = 0;
    }

    float horiz = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    if (p->on_ground && horiz > 0.3f && !p->flying)
        p->bob += dt * horiz * 1.8f;
    else
        p->bob += dt * 1.0f;

    if (p->on_ground && !was_ground && !jumped && !head_water) {
        /* Landing is worth a small camera dip. */
        p->bob += 0.35f;
    }
    if (jumped)
        p->bob += 0.2f;

    if (p->pos.y < -20) {
        player_damage(p, 6.0f);
        if (!p->dead) {
            p->pos.y = 100;
            p->vel = vec3(0, 0, 0);
        }
    }
}

static int sign_i(int v) {
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

bool player_raycast_ex(Player *p, World *w, float reach, bool hit_liquids) {
    Vec3 origin = player_eye(p);
    Vec3 dir = player_forward(p);
    p->target_valid = false;

    int x = (int)floorf(origin.x);
    int y = (int)floorf(origin.y);
    int z = (int)floorf(origin.z);
    int sx = sign_i((int)floorf(dir.x * 1000.0f));
    int sy = sign_i((int)floorf(dir.y * 1000.0f));
    int sz = sign_i((int)floorf(dir.z * 1000.0f));
    float tdx = fabsf(dir.x) > 1e-6f ? fabsf(1.0f / dir.x) : 1e30f;
    float tdy = fabsf(dir.y) > 1e-6f ? fabsf(1.0f / dir.y) : 1e30f;
    float tdz = fabsf(dir.z) > 1e-6f ? fabsf(1.0f / dir.z) : 1e30f;
    float tmx = dir.x > 0 ? ((float)(x + 1) - origin.x) * tdx
                          : (origin.x - (float)x) * tdx;
    float tmy = dir.y > 0 ? ((float)(y + 1) - origin.y) * tdy
                          : (origin.y - (float)y) * tdy;
    float tmz = dir.z > 0 ? ((float)(z + 1) - origin.z) * tdz
                          : (origin.z - (float)z) * tdz;
    int nx = 0, ny = 0, nz = 0;
    float dist = 0;

    for (int i = 0; i < 160 && dist <= reach; i++) {
        uint8_t b = world_get_block(w, x, y, z);
        if (b != BLOCK_AIR && (hit_liquids || !block_is_liquid(b))) {
            p->target_valid = true;
            p->target_x = x; p->target_y = y; p->target_z = z;
            p->target_nx = nx; p->target_ny = ny; p->target_nz = nz;
            p->target_block = b;
            return true;
        }
        if (tmx < tmy && tmx < tmz) {
            x += sx; dist = tmx; tmx += tdx; nx = -sx; ny = 0; nz = 0;
        } else if (tmy < tmz) {
            y += sy; dist = tmy; tmy += tdy; nx = 0; ny = -sy; nz = 0;
        } else {
            z += sz; dist = tmz; tmz += tdz; nx = 0; ny = 0; nz = -sz;
        }
    }
    return false;
}

bool player_raycast(Player *p, World *w, float reach) {
    return player_raycast_ex(p, w, reach, false);
}

int player_break(Player *p, World *w) {
    if (!player_raycast(p, w, 6.0f))
        return 0;
    if (p->target_block == BLOCK_BEDROCK)
        return 0;
    int bx = p->target_x, by = p->target_y, bz = p->target_z;
    uint8_t old = p->target_block;
    world_set_block(w, bx, by, bz, BLOCK_AIR, true);
    p->target_valid = false;
    return (int)old;
}

bool player_place(Player *p, World *w, uint8_t block) {
    if (block == BLOCK_AIR)
        return false;
    if (!player_raycast(p, w, 6.0f))
        return false;
    int x = p->target_x + p->target_nx;
    int y = p->target_y + p->target_ny;
    int z = p->target_z + p->target_nz;
    if (y < 0 || y >= WORLD_HEIGHT)
        return false;
    uint8_t cur = world_get_block(w, x, y, z);
    if (cur != BLOCK_AIR && !g_block_defs[cur].replaceable)
        return false;
    if (g_block_defs[block].solid && player_aabb_intersects_block(p, x, y, z))
        return false;
    world_set_block(w, x, y, z, block, true);
    return true;
}

static inline bool is_plank_id(uint8_t id) {
    return id >= BLOCK_OAK_PLANKS && id <= BLOCK_DARK_OAK_PLANKS;
}

static inline bool is_log_id(uint8_t id) {
    return id >= BLOCK_OAK_LOG && id <= BLOCK_DARK_OAK_LOG;
}

static inline bool is_cobble_id(uint8_t id) {
    return id == BLOCK_COBBLESTONE || id == BLOCK_COBBLED_DEEPSLATE;
}

static inline bool is_coal_id(uint8_t id) {
    return id == ITEM_COAL || id == ITEM_CHARCOAL;
}

void player_update_crafting(Player *p) {
    uint8_t c0 = p->crafting[0];
    uint8_t c1 = p->crafting[1];
    uint8_t c2 = p->crafting[2];
    uint8_t c3 = p->crafting[3];

    p->craft_result = BLOCK_AIR;
    p->craft_result_count = 0;

    int non_empty = (c0 != 0) + (c1 != 0) + (c2 != 0) + (c3 != 0);
    if (non_empty == 1) {
        uint8_t item = c0 ? c0 : (c1 ? c1 : (c2 ? c2 : c3));
        if (is_log_id(item)) {
            int offset = item - BLOCK_OAK_LOG;
            p->craft_result = (uint8_t)(BLOCK_OAK_PLANKS + offset);
            p->craft_result_count = 4;
            return;
        }
        if (item == BLOCK_PUMPKIN) {
            p->craft_result = BLOCK_CARVED_PUMPKIN;
            p->craft_result_count = 1;
            return;
        }
        if (item == BLOCK_IRON_BLOCK) {
            p->craft_result = ITEM_IRON_INGOT; p->craft_result_count = 9; return;
        }
        if (item == BLOCK_GOLD_BLOCK) {
            p->craft_result = ITEM_GOLD_INGOT; p->craft_result_count = 9; return;
        }
        if (item == BLOCK_DIAMOND_BLOCK) {
            p->craft_result = ITEM_DIAMOND; p->craft_result_count = 9; return;
        }
        if (item == BLOCK_COAL_BLOCK) {
            p->craft_result = ITEM_COAL; p->craft_result_count = 9; return;
        }
        if (item == ITEM_SUGAR_CANE) {
            p->craft_result = ITEM_SUGAR; p->craft_result_count = 1; return;
        }
    } else if (non_empty == 2) {
        /* Sticks: 2 vertical planks */
        bool sticks1 = (is_plank_id(c0) && is_plank_id(c2) && !c1 && !c3);
        bool sticks2 = (is_plank_id(c1) && is_plank_id(c3) && !c0 && !c2);
        if (sticks1 || sticks2) {
            p->craft_result = ITEM_STICK;
            p->craft_result_count = 4;
            return;
        }
        /* Torches: coal above stick */
        bool torch1 = (is_coal_id(c0) && c2 == ITEM_STICK && !c1 && !c3);
        bool torch2 = (is_coal_id(c1) && c3 == ITEM_STICK && !c0 && !c2);
        if (torch1 || torch2) {
            p->craft_result = BLOCK_TORCH;
            p->craft_result_count = 4;
            return;
        }
    } else if (non_empty == 4) {
        bool all_planks = is_plank_id(c0) && is_plank_id(c1) && is_plank_id(c2) && is_plank_id(c3);
        if (all_planks) {
            p->craft_result = BLOCK_CRAFTING_TABLE;
            p->craft_result_count = 1;
            return;
        }
        if (c0 == BLOCK_SAND && c1 == BLOCK_SAND && c2 == BLOCK_SAND && c3 == BLOCK_SAND) {
            p->craft_result = BLOCK_SANDSTONE;
            p->craft_result_count = 1;
            return;
        }
        if (c0 == BLOCK_CLAY && c1 == BLOCK_CLAY && c2 == BLOCK_CLAY && c3 == BLOCK_CLAY) {
            p->craft_result = BLOCK_BRICKS;
            p->craft_result_count = 1;
            return;
        }
        if (c0 == BLOCK_STONE && c1 == BLOCK_STONE && c2 == BLOCK_STONE && c3 == BLOCK_STONE) {
            p->craft_result = BLOCK_STONE_BRICKS;
            p->craft_result_count = 4;
            return;
        }
        if (is_cobble_id(c0) && is_cobble_id(c1) && is_cobble_id(c2) && is_cobble_id(c3)) {
            p->craft_result = BLOCK_FURNACE;
            p->craft_result_count = 1;
            return;
        }
    }
}

void player_update_crafting_3x3(Player *p) {
    p->craft_3x3_result = BLOCK_AIR;
    p->craft_3x3_result_count = 0;

    int non_empty = 0;
    int first_idx = -1;
    for (int i = 0; i < 9; i++) {
        if (p->crafting_3x3[i] != BLOCK_AIR) {
            non_empty++;
            if (first_idx < 0) first_idx = i;
        }
    }

    if (non_empty == 0) return;

    /* 1 item recipes */
    if (non_empty == 1) {
        uint8_t item = p->crafting_3x3[first_idx];
        if (is_log_id(item)) {
            p->craft_3x3_result = (uint8_t)(BLOCK_OAK_PLANKS + (item - BLOCK_OAK_LOG));
            p->craft_3x3_result_count = 4;
            return;
        }
        if (item == BLOCK_PUMPKIN) {
            p->craft_3x3_result = BLOCK_CARVED_PUMPKIN;
            p->craft_3x3_result_count = 1;
            return;
        }
        if (item == BLOCK_IRON_BLOCK) {
            p->craft_3x3_result = ITEM_IRON_INGOT; p->craft_3x3_result_count = 9; return;
        }
        if (item == BLOCK_GOLD_BLOCK) {
            p->craft_3x3_result = ITEM_GOLD_INGOT; p->craft_3x3_result_count = 9; return;
        }
        if (item == BLOCK_DIAMOND_BLOCK) {
            p->craft_3x3_result = ITEM_DIAMOND; p->craft_3x3_result_count = 9; return;
        }
        if (item == BLOCK_COAL_BLOCK) {
            p->craft_3x3_result = ITEM_COAL; p->craft_3x3_result_count = 9; return;
        }
        if (item == ITEM_SUGAR_CANE) {
            p->craft_3x3_result = ITEM_SUGAR; p->craft_3x3_result_count = 1; return;
        }
    }

    /* 2 items recipes */
    if (non_empty == 2) {
        /* Sticks: 2 vertical planks */
        int pairs[6][2] = {{0,3}, {1,4}, {2,5}, {3,6}, {4,7}, {5,8}};
        for (int k = 0; k < 6; k++) {
            int a = pairs[k][0], b = pairs[k][1];
            if (is_plank_id(p->crafting_3x3[a]) && is_plank_id(p->crafting_3x3[b])) {
                p->craft_3x3_result = ITEM_STICK;
                p->craft_3x3_result_count = 4;
                return;
            }
            if (is_coal_id(p->crafting_3x3[a]) && p->crafting_3x3[b] == ITEM_STICK) {
                p->craft_3x3_result = BLOCK_TORCH;
                p->craft_3x3_result_count = 4;
                return;
            }
            if (p->crafting_3x3[a] == ITEM_REDSTONE && p->crafting_3x3[b] == ITEM_STICK) {
                p->craft_3x3_result = BLOCK_REDSTONE_TORCH;
                p->craft_3x3_result_count = 1;
                return;
            }
            if (p->crafting_3x3[a] == ITEM_STICK && is_cobble_id(p->crafting_3x3[b])) {
                p->craft_3x3_result = BLOCK_LEVER;
                p->craft_3x3_result_count = 1;
                return;
            }
        }
    }

    /* 3 items recipes */
    if (non_empty == 3) {
        /* Bread: 3 wheat in horizontal row */
        int rows[3][3] = {{0,1,2}, {3,4,5}, {6,7,8}};
        for (int r = 0; r < 3; r++) {
            if (p->crafting_3x3[rows[r][0]] == ITEM_WHEAT &&
                p->crafting_3x3[rows[r][1]] == ITEM_WHEAT &&
                p->crafting_3x3[rows[r][2]] == ITEM_WHEAT) {
                p->craft_3x3_result = ITEM_BREAD;
                p->craft_3x3_result_count = 1;
                return;
            }
            if (p->crafting_3x3[rows[r][0]] == ITEM_SUGAR_CANE &&
                p->crafting_3x3[rows[r][1]] == ITEM_SUGAR_CANE &&
                p->crafting_3x3[rows[r][2]] == ITEM_SUGAR_CANE) {
                p->craft_3x3_result = ITEM_PAPER;
                p->craft_3x3_result_count = 3;
                return;
            }
            uint8_t sa = p->crafting_3x3[rows[r][0]];
            uint8_t sb = p->crafting_3x3[rows[r][1]];
            uint8_t sc = p->crafting_3x3[rows[r][2]];
            if (sa != BLOCK_AIR && sa == sb && sb == sc) {
                if (is_plank_id(sa)) { p->craft_3x3_result = BLOCK_OAK_SLAB; p->craft_3x3_result_count = 6; return; }
                if (sa == BLOCK_COBBLESTONE) { p->craft_3x3_result = BLOCK_COBBLESTONE_SLAB; p->craft_3x3_result_count = 6; return; }
                if (sa == BLOCK_STONE) { p->craft_3x3_result = BLOCK_STONE_SLAB; p->craft_3x3_result_count = 6; return; }
                if (sa == BLOCK_STONE_BRICKS) { p->craft_3x3_result = BLOCK_STONE_BRICK_SLAB; p->craft_3x3_result_count = 6; return; }
            }
        }

        /* Bucket: 3 iron ingots in V-shape */
        if ((p->crafting_3x3[0] == ITEM_IRON_INGOT && p->crafting_3x3[4] == ITEM_IRON_INGOT && p->crafting_3x3[2] == ITEM_IRON_INGOT) ||
            (p->crafting_3x3[3] == ITEM_IRON_INGOT && p->crafting_3x3[7] == ITEM_IRON_INGOT && p->crafting_3x3[5] == ITEM_IRON_INGOT)) {
            p->craft_3x3_result = ITEM_BUCKET;
            p->craft_3x3_result_count = 1;
            return;
        }

        /* Shovels: 1 material above 2 sticks in column 0, 1, or 2 */
        int cols[3][3] = {{0,3,6}, {1,4,7}, {2,5,8}};
        for (int c = 0; c < 3; c++) {
            uint8_t m = p->crafting_3x3[cols[c][0]];
            if (p->crafting_3x3[cols[c][1]] == ITEM_STICK &&
                p->crafting_3x3[cols[c][2]] == ITEM_STICK) {
                if (is_plank_id(m)) {
                    p->craft_3x3_result = ITEM_WOODEN_SHOVEL; p->craft_3x3_result_count = 1; return;
                }
                if (is_cobble_id(m)) {
                    p->craft_3x3_result = ITEM_STONE_SHOVEL; p->craft_3x3_result_count = 1; return;
                }
                if (m == ITEM_IRON_INGOT) {
                    p->craft_3x3_result = ITEM_IRON_SHOVEL; p->craft_3x3_result_count = 1; return;
                }
                if (m == ITEM_DIAMOND) {
                    p->craft_3x3_result = ITEM_DIAMOND_SHOVEL; p->craft_3x3_result_count = 1; return;
                }
            }
            /* Swords: 2 materials above 1 stick */
            uint8_t m1 = p->crafting_3x3[cols[c][0]];
            uint8_t m2 = p->crafting_3x3[cols[c][1]];
            if (p->crafting_3x3[cols[c][2]] == ITEM_STICK && m1 == m2) {
                if (is_plank_id(m1)) {
                    p->craft_3x3_result = ITEM_WOODEN_SWORD; p->craft_3x3_result_count = 1; return;
                }
                if (is_cobble_id(m1)) {
                    p->craft_3x3_result = ITEM_STONE_SWORD; p->craft_3x3_result_count = 1; return;
                }
                if (m1 == ITEM_IRON_INGOT) {
                    p->craft_3x3_result = ITEM_IRON_SWORD; p->craft_3x3_result_count = 1; return;
                }
                if (m1 == ITEM_DIAMOND) {
                    p->craft_3x3_result = ITEM_DIAMOND_SWORD; p->craft_3x3_result_count = 1; return;
                }
            }
        }
    }

    /* 4 items recipes */
    if (non_empty == 4) {
        int grids[4][4] = {{0,1,3,4}, {1,2,4,5}, {3,4,6,7}, {4,5,7,8}};
        for (int g = 0; g < 4; g++) {
            uint8_t a = p->crafting_3x3[grids[g][0]];
            uint8_t b = p->crafting_3x3[grids[g][1]];
            uint8_t c = p->crafting_3x3[grids[g][2]];
            uint8_t d = p->crafting_3x3[grids[g][3]];
            if (is_plank_id(a) && is_plank_id(b) && is_plank_id(c) && is_plank_id(d)) {
                p->craft_3x3_result = BLOCK_CRAFTING_TABLE; p->craft_3x3_result_count = 1; return;
            }
            if (a == BLOCK_SAND && b == BLOCK_SAND && c == BLOCK_SAND && d == BLOCK_SAND) {
                p->craft_3x3_result = BLOCK_SANDSTONE; p->craft_3x3_result_count = 1; return;
            }
            if (a == BLOCK_CLAY && b == BLOCK_CLAY && c == BLOCK_CLAY && d == BLOCK_CLAY) {
                p->craft_3x3_result = BLOCK_BRICKS; p->craft_3x3_result_count = 1; return;
            }
            if (a == BLOCK_STONE && b == BLOCK_STONE && c == BLOCK_STONE && d == BLOCK_STONE) {
                p->craft_3x3_result = BLOCK_STONE_BRICKS; p->craft_3x3_result_count = 4; return;
            }
        }
        /* Boots */
        if ((p->crafting_3x3[0] != BLOCK_AIR && p->crafting_3x3[2] != BLOCK_AIR &&
             p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR) ||
            (p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR &&
             p->crafting_3x3[6] != BLOCK_AIR && p->crafting_3x3[8] != BLOCK_AIR)) {
            uint8_t m = (p->crafting_3x3[0] != BLOCK_AIR) ? p->crafting_3x3[0] : p->crafting_3x3[3];
            if (m == ITEM_IRON_INGOT) { p->craft_3x3_result = ITEM_IRON_BOOTS; p->craft_3x3_result_count = 1; return; }
            if (m == ITEM_DIAMOND) { p->craft_3x3_result = ITEM_DIAMOND_BOOTS; p->craft_3x3_result_count = 1; return; }
        }
    }

    /* 5 items recipes: Pickaxes, Axes, Helmet */
    if (non_empty == 5) {
        /* Helmet */
        if ((p->crafting_3x3[0] != BLOCK_AIR && p->crafting_3x3[1] != BLOCK_AIR && p->crafting_3x3[2] != BLOCK_AIR &&
             p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR) ||
            (p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[4] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR &&
             p->crafting_3x3[6] != BLOCK_AIR && p->crafting_3x3[8] != BLOCK_AIR)) {
            uint8_t m = (p->crafting_3x3[0] != BLOCK_AIR) ? p->crafting_3x3[0] : p->crafting_3x3[3];
            if (m == ITEM_IRON_INGOT) { p->craft_3x3_result = ITEM_IRON_HELMET; p->craft_3x3_result_count = 1; return; }
            if (m == ITEM_DIAMOND) { p->craft_3x3_result = ITEM_DIAMOND_HELMET; p->craft_3x3_result_count = 1; return; }
        }
        /* Redstone Lamp: glowstone surrounded by 4 redstone dust */
        if (p->crafting_3x3[4] == BLOCK_GLOWSTONE &&
            p->crafting_3x3[1] == ITEM_REDSTONE && p->crafting_3x3[3] == ITEM_REDSTONE &&
            p->crafting_3x3[5] == ITEM_REDSTONE && p->crafting_3x3[7] == ITEM_REDSTONE) {
            p->craft_3x3_result = BLOCK_REDSTONE_LAMP_OFF;
            p->craft_3x3_result_count = 1;
            return;
        }

        /* Pickaxe: top row 3 mats, sticks at 4 and 7 */
        uint8_t m0 = p->crafting_3x3[0];
        uint8_t m1 = p->crafting_3x3[1];
        uint8_t m2 = p->crafting_3x3[2];
        if (m0 != BLOCK_AIR && m0 == m1 && m1 == m2 &&
            p->crafting_3x3[4] == ITEM_STICK && p->crafting_3x3[7] == ITEM_STICK) {
            if (is_plank_id(m0)) {
                p->craft_3x3_result = ITEM_WOODEN_PICKAXE; p->craft_3x3_result_count = 1; return;
            }
            if (is_cobble_id(m0)) {
                p->craft_3x3_result = ITEM_STONE_PICKAXE; p->craft_3x3_result_count = 1; return;
            }
            if (m0 == ITEM_IRON_INGOT) {
                p->craft_3x3_result = ITEM_IRON_PICKAXE; p->craft_3x3_result_count = 1; return;
            }
            if (m0 == ITEM_DIAMOND) {
                p->craft_3x3_result = ITEM_DIAMOND_PICKAXE; p->craft_3x3_result_count = 1; return;
            }
        }

        /* Axe: shape A (0,1,3 mat, 4 stick, 7 stick) or shape B (1,2,5 mat, 4 stick, 7 stick) */
        if (p->crafting_3x3[4] == ITEM_STICK && p->crafting_3x3[7] == ITEM_STICK) {
            uint8_t ma = p->crafting_3x3[0];
            if (ma != BLOCK_AIR && p->crafting_3x3[1] == ma && p->crafting_3x3[3] == ma) {
                if (is_plank_id(ma)) {
                    p->craft_3x3_result = ITEM_WOODEN_AXE; p->craft_3x3_result_count = 1; return;
                }
                if (is_cobble_id(ma)) {
                    p->craft_3x3_result = ITEM_STONE_AXE; p->craft_3x3_result_count = 1; return;
                }
                if (ma == ITEM_IRON_INGOT) {
                    p->craft_3x3_result = ITEM_IRON_AXE; p->craft_3x3_result_count = 1; return;
                }
                if (ma == ITEM_DIAMOND) {
                    p->craft_3x3_result = ITEM_DIAMOND_AXE; p->craft_3x3_result_count = 1; return;
                }
            }
            uint8_t mb = p->crafting_3x3[1];
            if (mb != BLOCK_AIR && p->crafting_3x3[2] == mb && p->crafting_3x3[5] == mb) {
                if (is_plank_id(mb)) {
                    p->craft_3x3_result = ITEM_WOODEN_AXE; p->craft_3x3_result_count = 1; return;
                }
                if (is_cobble_id(mb)) {
                    p->craft_3x3_result = ITEM_STONE_AXE; p->craft_3x3_result_count = 1; return;
                }
                if (mb == ITEM_IRON_INGOT) {
                    p->craft_3x3_result = ITEM_IRON_AXE; p->craft_3x3_result_count = 1; return;
                }
                if (mb == ITEM_DIAMOND) {
                    p->craft_3x3_result = ITEM_DIAMOND_AXE; p->craft_3x3_result_count = 1; return;
                }
            }
        }
    }

    /* 6 items recipes: Stairs */
    if (non_empty == 6) {
        /* Doors: 2 vertical columns of 3 */
        if ((p->crafting_3x3[0] != BLOCK_AIR && p->crafting_3x3[1] != BLOCK_AIR &&
             p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[4] != BLOCK_AIR &&
             p->crafting_3x3[6] != BLOCK_AIR && p->crafting_3x3[7] != BLOCK_AIR &&
             p->crafting_3x3[2] == BLOCK_AIR && p->crafting_3x3[5] == BLOCK_AIR && p->crafting_3x3[8] == BLOCK_AIR) ||
            (p->crafting_3x3[1] != BLOCK_AIR && p->crafting_3x3[2] != BLOCK_AIR &&
             p->crafting_3x3[4] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR &&
             p->crafting_3x3[7] != BLOCK_AIR && p->crafting_3x3[8] != BLOCK_AIR &&
             p->crafting_3x3[0] == BLOCK_AIR && p->crafting_3x3[3] == BLOCK_AIR && p->crafting_3x3[6] == BLOCK_AIR)) {
            uint8_t m = (p->crafting_3x3[0] != BLOCK_AIR) ? p->crafting_3x3[0] : p->crafting_3x3[1];
            if (is_plank_id(m)) { p->craft_3x3_result = BLOCK_OAK_DOOR; p->craft_3x3_result_count = 3; return; }
            if (m == ITEM_IRON_INGOT) { p->craft_3x3_result = BLOCK_IRON_DOOR; p->craft_3x3_result_count = 3; return; }
        }
        /* Trapdoor: 2 horizontal rows of 3 */
        if ((p->crafting_3x3[0] != BLOCK_AIR && p->crafting_3x3[1] != BLOCK_AIR && p->crafting_3x3[2] != BLOCK_AIR &&
             p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[4] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR &&
             p->crafting_3x3[6] == BLOCK_AIR && p->crafting_3x3[7] == BLOCK_AIR && p->crafting_3x3[8] == BLOCK_AIR) ||
            (p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[4] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR &&
             p->crafting_3x3[6] != BLOCK_AIR && p->crafting_3x3[7] != BLOCK_AIR && p->crafting_3x3[8] != BLOCK_AIR &&
             p->crafting_3x3[0] == BLOCK_AIR && p->crafting_3x3[1] == BLOCK_AIR && p->crafting_3x3[2] == BLOCK_AIR)) {
            uint8_t m = p->crafting_3x3[3];
            if (is_plank_id(m)) { p->craft_3x3_result = BLOCK_OAK_TRAPDOOR; p->craft_3x3_result_count = 2; return; }
        }

        /* Stairs left-aligned:
           M . .
           M M .
           M M M
        */
        if (p->crafting_3x3[0] != BLOCK_AIR &&
            p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[4] != BLOCK_AIR &&
            p->crafting_3x3[6] != BLOCK_AIR && p->crafting_3x3[7] != BLOCK_AIR && p->crafting_3x3[8] != BLOCK_AIR) {
            uint8_t m = p->crafting_3x3[0];
            if (p->crafting_3x3[3] == m && p->crafting_3x3[4] == m &&
                p->crafting_3x3[6] == m && p->crafting_3x3[7] == m && p->crafting_3x3[8] == m) {
                if (is_plank_id(m)) { p->craft_3x3_result = BLOCK_OAK_STAIRS; p->craft_3x3_result_count = 4; return; }
                if (m == BLOCK_COBBLESTONE) { p->craft_3x3_result = BLOCK_COBBLESTONE_STAIRS; p->craft_3x3_result_count = 4; return; }
                if (m == BLOCK_STONE_BRICKS) { p->craft_3x3_result = BLOCK_STONE_BRICK_STAIRS; p->craft_3x3_result_count = 4; return; }
            }
        }
        /* Stairs right-aligned:
           . . M
           . M M
           M M M
        */
        if (p->crafting_3x3[2] != BLOCK_AIR &&
            p->crafting_3x3[4] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR &&
            p->crafting_3x3[6] != BLOCK_AIR && p->crafting_3x3[7] != BLOCK_AIR && p->crafting_3x3[8] != BLOCK_AIR) {
            uint8_t m = p->crafting_3x3[2];
            if (p->crafting_3x3[4] == m && p->crafting_3x3[5] == m &&
                p->crafting_3x3[6] == m && p->crafting_3x3[7] == m && p->crafting_3x3[8] == m) {
                if (is_plank_id(m)) { p->craft_3x3_result = BLOCK_OAK_STAIRS; p->craft_3x3_result_count = 4; return; }
                if (m == BLOCK_COBBLESTONE) { p->craft_3x3_result = BLOCK_COBBLESTONE_STAIRS; p->craft_3x3_result_count = 4; return; }
                if (m == BLOCK_STONE_BRICKS) { p->craft_3x3_result = BLOCK_STONE_BRICK_STAIRS; p->craft_3x3_result_count = 4; return; }
            }
        }
    }

    /* 7 items recipes: Leggings */
    if (non_empty == 7) {
        if (p->crafting_3x3[0] != BLOCK_AIR && p->crafting_3x3[1] != BLOCK_AIR && p->crafting_3x3[2] != BLOCK_AIR &&
            p->crafting_3x3[3] != BLOCK_AIR && p->crafting_3x3[5] != BLOCK_AIR &&
            p->crafting_3x3[6] != BLOCK_AIR && p->crafting_3x3[8] != BLOCK_AIR) {
            uint8_t m = p->crafting_3x3[0];
            if (m == ITEM_IRON_INGOT) { p->craft_3x3_result = ITEM_IRON_LEGGINGS; p->craft_3x3_result_count = 1; return; }
            if (m == ITEM_DIAMOND) { p->craft_3x3_result = ITEM_DIAMOND_LEGGINGS; p->craft_3x3_result_count = 1; return; }
        }
    }

    /* 8 items recipes: Furnace, Chest, Chestplate */
    if (non_empty == 8) {
        if (p->crafting_3x3[1] == BLOCK_AIR) {
            uint8_t m = p->crafting_3x3[0];
            bool all_mat = true;
            int chest_slots[8] = {0, 2, 3, 4, 5, 6, 7, 8};
            for (int i = 0; i < 8; i++) {
                if (p->crafting_3x3[chest_slots[i]] != m) { all_mat = false; break; }
            }
            if (all_mat) {
                if (m == ITEM_IRON_INGOT) { p->craft_3x3_result = ITEM_IRON_CHESTPLATE; p->craft_3x3_result_count = 1; return; }
                if (m == ITEM_DIAMOND) { p->craft_3x3_result = ITEM_DIAMOND_CHESTPLATE; p->craft_3x3_result_count = 1; return; }
            }
        }
        if (p->crafting_3x3[4] == BLOCK_AIR) {
            bool all_cobble = true;
            bool all_planks = true;
            int ring[8] = {0, 1, 2, 3, 5, 6, 7, 8};
            for (int i = 0; i < 8; i++) {
                uint8_t item = p->crafting_3x3[ring[i]];
                if (!is_cobble_id(item)) all_cobble = false;
                if (!is_plank_id(item)) all_planks = false;
            }
            if (all_cobble) {
                p->craft_3x3_result = BLOCK_FURNACE; p->craft_3x3_result_count = 1; return;
            }
            if (all_planks) {
                p->craft_3x3_result = BLOCK_CHEST; p->craft_3x3_result_count = 1; return;
            }
        }
    }

    /* 9 items recipes: Blocks from materials */
    if (non_empty == 9) {
        uint8_t m0 = p->crafting_3x3[0];
        bool all_same = true;
        for (int i = 1; i < 9; i++) {
            if (p->crafting_3x3[i] != m0) { all_same = false; break; }
        }
        if (all_same) {
            if (m0 == ITEM_IRON_INGOT) {
                p->craft_3x3_result = BLOCK_IRON_BLOCK; p->craft_3x3_result_count = 1; return;
            }
            if (m0 == ITEM_GOLD_INGOT) {
                p->craft_3x3_result = BLOCK_GOLD_BLOCK; p->craft_3x3_result_count = 1; return;
            }
            if (m0 == ITEM_DIAMOND) {
                p->craft_3x3_result = BLOCK_DIAMOND_BLOCK; p->craft_3x3_result_count = 1; return;
            }
            if (m0 == ITEM_COAL) {
                p->craft_3x3_result = BLOCK_COAL_BLOCK; p->craft_3x3_result_count = 1; return;
            }
        }
    }
}

void player_update_furnace(Player *p, float dt) {
    if (p->furnace_burn_time > 0.0f) {
        p->furnace_burn_time -= dt;
        if (p->furnace_burn_time < 0.0f) {
            p->furnace_burn_time = 0.0f;
        }
    }

    uint8_t res = item_smelt_result(p->furnace_input);
    bool can_smelt = (res != BLOCK_AIR) &&
                     (p->furnace_output == BLOCK_AIR ||
                      (p->furnace_output == res && p->furnace_output_count < 64));

    if (can_smelt) {
        if (p->furnace_burn_time <= 0.0f) {
            float fuel_dur = 0.0f;
            if (p->furnace_fuel != BLOCK_AIR && p->furnace_fuel_count > 0 &&
                item_is_fuel(p->furnace_fuel, &fuel_dur)) {
                bool was_lava = (p->furnace_fuel == ITEM_LAVA_BUCKET);
                p->furnace_fuel_count--;
                if (p->furnace_fuel_count == 0) {
                    p->furnace_fuel = was_lava ? ITEM_BUCKET : BLOCK_AIR;
                    if (was_lava) p->furnace_fuel_count = 1;
                }
                p->furnace_burn_time = fuel_dur;
                p->furnace_max_burn_time = fuel_dur;
            }
        }

        if (p->furnace_burn_time > 0.0f) {
            p->furnace_cook_time += dt;
            if (p->furnace_cook_time >= 10.0f) {
                p->furnace_cook_time = 0.0f;
                if (p->furnace_output == BLOCK_AIR) {
                    p->furnace_output = res;
                    p->furnace_output_count = 1;
                } else if (p->furnace_output == res) {
                    p->furnace_output_count++;
                }
                p->furnace_input_count--;
                if (p->furnace_input_count == 0) {
                    p->furnace_input = BLOCK_AIR;
                }
            }
        } else {
            if (p->furnace_cook_time > 0.0f) {
                p->furnace_cook_time -= dt * 2.0f;
                if (p->furnace_cook_time < 0.0f) p->furnace_cook_time = 0.0f;
            }
        }
    } else {
        if (p->furnace_cook_time > 0.0f) {
            p->furnace_cook_time -= dt * 2.0f;
            if (p->furnace_cook_time < 0.0f) p->furnace_cook_time = 0.0f;
        }
    }
}

void player_slot_click(Player *p, SlotType type, int index, bool right_click, bool shift_click) {
    uint8_t *slot_item = NULL;
    uint8_t *slot_count = NULL;

    switch (type) {
    case SLOT_HOTBAR:
        if (index >= 0 && index < HOTBAR_SLOTS) {
            slot_item = &p->hotbar[index];
            slot_count = &p->hotbar_counts[index];
        }
        break;
    case SLOT_INVENTORY:
        if (index >= 0 && index < INVENTORY_SLOTS) {
            slot_item = &p->inventory[index];
            slot_count = &p->inventory_counts[index];
        }
        break;
    case SLOT_ARMOR:
        if (index >= 0 && index < ARMOR_SLOTS) {
            slot_item = &p->armor[index];
        }
        break;
    case SLOT_CRAFTING:
        if (index >= 0 && index < CRAFTING_SLOTS) {
            slot_item = &p->crafting[index];
            slot_count = &p->crafting_counts[index];
        }
        break;
    case SLOT_CRAFT_RESULT:
        if (p->craft_result != BLOCK_AIR) {
            if (p->cursor_item == BLOCK_AIR) {
                p->cursor_item = p->craft_result;
                p->cursor_count = p->craft_result_count;
            } else if (p->cursor_item == p->craft_result && p->cursor_count + p->craft_result_count <= 64) {
                p->cursor_count += p->craft_result_count;
            } else {
                return;
            }
            for (int i = 0; i < 4; i++) {
                if (p->crafting[i] != BLOCK_AIR) {
                    if (p->crafting_counts[i] > 1) {
                        p->crafting_counts[i]--;
                    } else {
                        p->crafting[i] = BLOCK_AIR;
                        p->crafting_counts[i] = 0;
                    }
                }
            }
            player_update_crafting(p);
            return;
        }
        return;
    case SLOT_OFFHAND:
        slot_item = &p->offhand;
        break;
    case SLOT_NONE:
    case SLOT_CREATIVE_PALETTE:
    default:
        return;
    }

    if (!slot_item) return;

    if (type == SLOT_ARMOR && p->cursor_item != BLOCK_AIR) {
        if (armor_slot_for_item(p->cursor_item) != index)
            return;
    }

    if (shift_click && *slot_item != BLOCK_AIR) {
        if (type == SLOT_ARMOR) {
            /* Shift-click from armor moves to inventory/hotbar */
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (p->hotbar[i] == BLOCK_AIR) {
                    p->hotbar[i] = *slot_item;
                    p->hotbar_counts[i] = 1;
                    *slot_item = BLOCK_AIR;
                    player_update_armor_points(p);
                    return;
                }
            }
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
                if (p->inventory[i] == BLOCK_AIR) {
                    p->inventory[i] = *slot_item;
                    p->inventory_counts[i] = 1;
                    *slot_item = BLOCK_AIR;
                    player_update_armor_points(p);
                    return;
                }
            }
            return;
        }

        if (item_is_armor(*slot_item)) {
            int aslot = armor_slot_for_item(*slot_item);
            if (aslot >= 0 && p->armor[aslot] == BLOCK_AIR) {
                p->armor[aslot] = *slot_item;
                *slot_item = BLOCK_AIR;
                if (slot_count) *slot_count = 0;
                player_update_armor_points(p);
                return;
            }
        }

        if (type == SLOT_HOTBAR) {
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
                if (p->inventory[i] == BLOCK_AIR) {
                    p->inventory[i] = *slot_item;
                    p->inventory_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        } else if (type == SLOT_INVENTORY) {
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (p->hotbar[i] == BLOCK_AIR) {
                    p->hotbar[i] = *slot_item;
                    p->hotbar_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        }
        return;
    }

    if (p->cursor_item == BLOCK_AIR) {
        if (*slot_item != BLOCK_AIR) {
            if (right_click && slot_count && *slot_count > 1) {
                uint8_t half = (*slot_count + 1) / 2;
                p->cursor_item = *slot_item;
                p->cursor_count = half;
                *slot_count -= half;
            } else {
                p->cursor_item = *slot_item;
                p->cursor_count = slot_count ? *slot_count : 1;
                *slot_item = BLOCK_AIR;
                if (slot_count) *slot_count = 0;
            }
        }
    } else {
        if (*slot_item == BLOCK_AIR) {
            if (right_click) {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = 1;
                p->cursor_count--;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            } else {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = BLOCK_AIR;
                p->cursor_count = 0;
            }
        } else if (*slot_item == p->cursor_item && slot_count) {
            if (right_click) {
                if (*slot_count < 64) {
                    (*slot_count)++;
                    p->cursor_count--;
                    if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
                }
            } else {
                int add = 64 - *slot_count;
                if (add > p->cursor_count) add = p->cursor_count;
                *slot_count += (uint8_t)add;
                p->cursor_count -= (uint8_t)add;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            }
        } else {
            if (!right_click) {
                uint8_t temp_item = *slot_item;
                uint8_t temp_count = slot_count ? *slot_count : 1;
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = temp_item;
                p->cursor_count = temp_count;
            }
        }
    }

    if (type == SLOT_CRAFTING) {
        player_update_crafting(p);
    }
    if (type == SLOT_ARMOR) {
        player_update_armor_points(p);
    }
}

void player_slot_click_crafting_3x3(Player *p, SlotType type, int index, bool right_click, bool shift_click) {
    if (type == SLOT_CRAFT_3X3_RESULT) {
        if (p->craft_3x3_result != BLOCK_AIR) {
            if (p->cursor_item == BLOCK_AIR) {
                p->cursor_item = p->craft_3x3_result;
                p->cursor_count = p->craft_3x3_result_count;
            } else if (p->cursor_item == p->craft_3x3_result && p->cursor_count + p->craft_3x3_result_count <= 64) {
                p->cursor_count += p->craft_3x3_result_count;
            } else {
                return;
            }
            for (int i = 0; i < 9; i++) {
                if (p->crafting_3x3[i] != BLOCK_AIR) {
                    if (p->crafting_3x3_counts[i] > 1) {
                        p->crafting_3x3_counts[i]--;
                    } else {
                        p->crafting_3x3[i] = BLOCK_AIR;
                        p->crafting_3x3_counts[i] = 0;
                    }
                }
            }
            player_update_crafting_3x3(p);
            return;
        }
        return;
    }

    uint8_t *slot_item = NULL;
    uint8_t *slot_count = NULL;

    switch (type) {
    case SLOT_HOTBAR:
        if (index >= 0 && index < HOTBAR_SLOTS) {
            slot_item = &p->hotbar[index];
            slot_count = &p->hotbar_counts[index];
        }
        break;
    case SLOT_INVENTORY:
        if (index >= 0 && index < INVENTORY_SLOTS) {
            slot_item = &p->inventory[index];
            slot_count = &p->inventory_counts[index];
        }
        break;
    case SLOT_CRAFTING_3X3:
        if (index >= 0 && index < 9) {
            slot_item = &p->crafting_3x3[index];
            slot_count = &p->crafting_3x3_counts[index];
        }
        break;
    default:
        return;
    }

    if (!slot_item) return;

    if (type == SLOT_ARMOR && p->cursor_item != BLOCK_AIR) {
        if (armor_slot_for_item(p->cursor_item) != index)
            return;
    }

    if (shift_click && *slot_item != BLOCK_AIR) {
        if (type == SLOT_ARMOR) {
            /* Shift-click from armor moves to inventory/hotbar */
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (p->hotbar[i] == BLOCK_AIR) {
                    p->hotbar[i] = *slot_item;
                    p->hotbar_counts[i] = 1;
                    *slot_item = BLOCK_AIR;
                    player_update_armor_points(p);
                    return;
                }
            }
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
                if (p->inventory[i] == BLOCK_AIR) {
                    p->inventory[i] = *slot_item;
                    p->inventory_counts[i] = 1;
                    *slot_item = BLOCK_AIR;
                    player_update_armor_points(p);
                    return;
                }
            }
            return;
        }

        if (item_is_armor(*slot_item)) {
            int aslot = armor_slot_for_item(*slot_item);
            if (aslot >= 0 && p->armor[aslot] == BLOCK_AIR) {
                p->armor[aslot] = *slot_item;
                *slot_item = BLOCK_AIR;
                if (slot_count) *slot_count = 0;
                player_update_armor_points(p);
                return;
            }
        }

        if (type == SLOT_HOTBAR) {
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
                if (p->inventory[i] == BLOCK_AIR) {
                    p->inventory[i] = *slot_item;
                    p->inventory_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        } else if (type == SLOT_INVENTORY) {
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (p->hotbar[i] == BLOCK_AIR) {
                    p->hotbar[i] = *slot_item;
                    p->hotbar_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        }
        return;
    }

    if (p->cursor_item == BLOCK_AIR) {
        if (*slot_item != BLOCK_AIR) {
            if (right_click && slot_count && *slot_count > 1) {
                uint8_t half = (*slot_count + 1) / 2;
                p->cursor_item = *slot_item;
                p->cursor_count = half;
                *slot_count -= half;
            } else {
                p->cursor_item = *slot_item;
                p->cursor_count = slot_count ? *slot_count : 1;
                *slot_item = BLOCK_AIR;
                if (slot_count) *slot_count = 0;
            }
        }
    } else {
        if (*slot_item == BLOCK_AIR) {
            if (right_click) {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = 1;
                p->cursor_count--;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            } else {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = BLOCK_AIR;
                p->cursor_count = 0;
            }
        } else if (*slot_item == p->cursor_item && slot_count) {
            if (right_click) {
                if (*slot_count < 64) {
                    (*slot_count)++;
                    p->cursor_count--;
                    if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
                }
            } else {
                int add = 64 - *slot_count;
                if (add > p->cursor_count) add = p->cursor_count;
                *slot_count += (uint8_t)add;
                p->cursor_count -= (uint8_t)add;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            }
        } else {
            if (!right_click) {
                uint8_t temp_item = *slot_item;
                uint8_t temp_count = slot_count ? *slot_count : 1;
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = temp_item;
                p->cursor_count = temp_count;
            }
        }
    }

    if (type == SLOT_CRAFTING_3X3) {
        player_update_crafting_3x3(p);
    }
}

void player_slot_click_furnace(Player *p, SlotType type, int index, bool right_click, bool shift_click) {
    if (type == SLOT_FURNACE_OUTPUT) {
        if (p->furnace_output != BLOCK_AIR && p->furnace_output_count > 0) {
            if (p->cursor_item == BLOCK_AIR) {
                p->cursor_item = p->furnace_output;
                p->cursor_count = p->furnace_output_count;
                p->furnace_output = BLOCK_AIR;
                p->furnace_output_count = 0;
            } else if (p->cursor_item == p->furnace_output && p->cursor_count + p->furnace_output_count <= 64) {
                p->cursor_count += p->furnace_output_count;
                p->furnace_output = BLOCK_AIR;
                p->furnace_output_count = 0;
            }
        }
        return;
    }

    uint8_t *slot_item = NULL;
    uint8_t *slot_count = NULL;

    switch (type) {
    case SLOT_HOTBAR:
        if (index >= 0 && index < HOTBAR_SLOTS) {
            slot_item = &p->hotbar[index];
            slot_count = &p->hotbar_counts[index];
        }
        break;
    case SLOT_INVENTORY:
        if (index >= 0 && index < INVENTORY_SLOTS) {
            slot_item = &p->inventory[index];
            slot_count = &p->inventory_counts[index];
        }
        break;
    case SLOT_FURNACE_INPUT:
        slot_item = &p->furnace_input;
        slot_count = &p->furnace_input_count;
        break;
    case SLOT_FURNACE_FUEL:
        slot_item = &p->furnace_fuel;
        slot_count = &p->furnace_fuel_count;
        break;
    default:
        return;
    }

    if (!slot_item) return;

    if (type == SLOT_ARMOR && p->cursor_item != BLOCK_AIR) {
        if (armor_slot_for_item(p->cursor_item) != index)
            return;
    }

    if (shift_click && *slot_item != BLOCK_AIR) {
        if (type == SLOT_ARMOR) {
            /* Shift-click from armor moves to inventory/hotbar */
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (p->hotbar[i] == BLOCK_AIR) {
                    p->hotbar[i] = *slot_item;
                    p->hotbar_counts[i] = 1;
                    *slot_item = BLOCK_AIR;
                    player_update_armor_points(p);
                    return;
                }
            }
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
                if (p->inventory[i] == BLOCK_AIR) {
                    p->inventory[i] = *slot_item;
                    p->inventory_counts[i] = 1;
                    *slot_item = BLOCK_AIR;
                    player_update_armor_points(p);
                    return;
                }
            }
            return;
        }

        if (item_is_armor(*slot_item)) {
            int aslot = armor_slot_for_item(*slot_item);
            if (aslot >= 0 && p->armor[aslot] == BLOCK_AIR) {
                p->armor[aslot] = *slot_item;
                *slot_item = BLOCK_AIR;
                if (slot_count) *slot_count = 0;
                player_update_armor_points(p);
                return;
            }
        }

        if (type == SLOT_HOTBAR) {
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
                if (p->inventory[i] == BLOCK_AIR) {
                    p->inventory[i] = *slot_item;
                    p->inventory_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        } else if (type == SLOT_INVENTORY) {
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (p->hotbar[i] == BLOCK_AIR) {
                    p->hotbar[i] = *slot_item;
                    p->hotbar_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        }
        return;
    }

    if (p->cursor_item == BLOCK_AIR) {
        if (*slot_item != BLOCK_AIR) {
            if (right_click && slot_count && *slot_count > 1) {
                uint8_t half = (*slot_count + 1) / 2;
                p->cursor_item = *slot_item;
                p->cursor_count = half;
                *slot_count -= half;
            } else {
                p->cursor_item = *slot_item;
                p->cursor_count = slot_count ? *slot_count : 1;
                *slot_item = BLOCK_AIR;
                if (slot_count) *slot_count = 0;
            }
        }
    } else {
        if (*slot_item == BLOCK_AIR) {
            if (right_click) {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = 1;
                p->cursor_count--;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            } else {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = BLOCK_AIR;
                p->cursor_count = 0;
            }
        } else if (*slot_item == p->cursor_item && slot_count) {
            if (right_click) {
                if (*slot_count < 64) {
                    (*slot_count)++;
                    p->cursor_count--;
                    if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
                }
            } else {
                int add = 64 - *slot_count;
                if (add > p->cursor_count) add = p->cursor_count;
                *slot_count += (uint8_t)add;
                p->cursor_count -= (uint8_t)add;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            }
        } else {
            if (!right_click) {
                uint8_t temp_item = *slot_item;
                uint8_t temp_count = slot_count ? *slot_count : 1;
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = temp_item;
                p->cursor_count = temp_count;
            }
        }
    }
}

void player_slot_click_chest(Player *p, SlotType type, int index, bool right_click, bool shift_click) {
    uint8_t *slot_item = NULL;
    uint8_t *slot_count = NULL;

    switch (type) {
    case SLOT_HOTBAR:
        if (index >= 0 && index < HOTBAR_SLOTS) {
            slot_item = &p->hotbar[index];
            slot_count = &p->hotbar_counts[index];
        }
        break;
    case SLOT_INVENTORY:
        if (index >= 0 && index < INVENTORY_SLOTS) {
            slot_item = &p->inventory[index];
            slot_count = &p->inventory_counts[index];
        }
        break;
    case SLOT_CHEST:
        if (index >= 0 && index < CHEST_SLOTS) {
            slot_item = &p->chest[index];
            slot_count = &p->chest_counts[index];
        }
        break;
    default:
        return;
    }

    if (!slot_item) return;

    if (shift_click && *slot_item != BLOCK_AIR) {
        if (type == SLOT_CHEST) {
            /* Move from chest into player hotbar or inventory */
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (p->hotbar[i] == BLOCK_AIR) {
                    p->hotbar[i] = *slot_item;
                    p->hotbar_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
                if (p->inventory[i] == BLOCK_AIR) {
                    p->inventory[i] = *slot_item;
                    p->inventory_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        } else {
            /* Move from hotbar/inventory into chest */
            for (int i = 0; i < CHEST_SLOTS; i++) {
                if (p->chest[i] == BLOCK_AIR) {
                    p->chest[i] = *slot_item;
                    p->chest_counts[i] = slot_count ? *slot_count : 1;
                    *slot_item = BLOCK_AIR;
                    if (slot_count) *slot_count = 0;
                    return;
                }
            }
        }
        return;
    }

    if (p->cursor_item == BLOCK_AIR) {
        if (*slot_item != BLOCK_AIR) {
            if (right_click && slot_count && *slot_count > 1) {
                uint8_t half = (*slot_count + 1) / 2;
                p->cursor_item = *slot_item;
                p->cursor_count = half;
                *slot_count -= half;
            } else {
                p->cursor_item = *slot_item;
                p->cursor_count = slot_count ? *slot_count : 1;
                *slot_item = BLOCK_AIR;
                if (slot_count) *slot_count = 0;
            }
        }
    } else {
        if (*slot_item == BLOCK_AIR) {
            if (right_click) {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = 1;
                p->cursor_count--;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            } else {
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = BLOCK_AIR;
                p->cursor_count = 0;
            }
        } else if (*slot_item == p->cursor_item && slot_count) {
            if (right_click) {
                if (*slot_count < 64) {
                    (*slot_count)++;
                    p->cursor_count--;
                    if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
                }
            } else {
                int add = 64 - *slot_count;
                if (add > p->cursor_count) add = p->cursor_count;
                *slot_count += (uint8_t)add;
                p->cursor_count -= (uint8_t)add;
                if (p->cursor_count == 0) p->cursor_item = BLOCK_AIR;
            }
        } else {
            if (!right_click) {
                uint8_t temp_item = *slot_item;
                uint8_t temp_count = slot_count ? *slot_count : 1;
                *slot_item = p->cursor_item;
                if (slot_count) *slot_count = p->cursor_count;
                p->cursor_item = temp_item;
                p->cursor_count = temp_count;
            }
        }
    }
}

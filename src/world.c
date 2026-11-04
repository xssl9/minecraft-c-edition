#include "world.h"

#include "mced.h"
#include "noise.h"

#include <errno.h>
#include <math.h>

#define WORLD_MAGIC "MCW1"

static int floor_div16(int v) {
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

static int floor_mod16(int v) __attribute__((unused));
static int floor_mod16(int v) {
    int m = v % 16;
    if (m < 0)
        m += 16;
    return m;
}

static void world_generate_chunk_for_edit(World *w, Chunk *c);

static Chunk *chunk_new(int cx, int cz) {
    Chunk *c = xcalloc(1, sizeof(Chunk));
    c->cx = cx;
    c->cz = cz;
    return c;
}

void world_init(World *w, uint64_t seed, int load_radius) {
    memset(w, 0, sizeof(*w));
    w->seed = seed;
    w->load_radius = load_radius > 0 ? load_radius : 6;
    w->last_pcx = 999999;
    w->last_pcz = 999999;
    w->fully_loaded = false;
}

void world_destroy(World *w) {
    for (int i = 0; i < w->chunk_count; i++)
        free(w->chunks[i]);
    free(w->chunks);
    free(w->chunk_hash);
    free(w->mods);
    memset(w, 0, sizeof(*w));
}

static void chunk_hash_insert(World *w, Chunk *c) {
    int mask = w->chunk_hash_capacity - 1;
    uint32_t slot = chunk_hash_key(c->cx, c->cz) & (uint32_t)mask;
    while (w->chunk_hash[slot])
        slot = (slot + 1) & (uint32_t)mask;
    w->chunk_hash[slot] = c;
}

static void chunk_hash_rebuild(World *w) {
    int cap = 64;
    while (cap < w->chunk_count * 4)
        cap *= 2;
    free(w->chunk_hash);
    w->chunk_hash = xcalloc((size_t)cap, sizeof(Chunk *));
    w->chunk_hash_capacity = cap;
    for (int i = 0; i < w->chunk_count; i++)
        chunk_hash_insert(w, w->chunks[i]);
}

static void world_add_chunk(World *w, Chunk *c) {
    if (w->chunk_count == w->chunk_capacity) {
        w->chunk_capacity = w->chunk_capacity ? w->chunk_capacity * 2 : 64;
        w->chunks = xrealloc(w->chunks, (size_t)w->chunk_capacity * sizeof(Chunk *));
    }
    w->chunks[w->chunk_count++] = c;
    if (w->chunk_count * 2 >= w->chunk_hash_capacity)
        chunk_hash_rebuild(w);
    else
        chunk_hash_insert(w, c);
}

Chunk *world_find_chunk(World *w, int cx, int cz, bool create) {
    if (w->chunk_hash_capacity > 0) {
        int mask = w->chunk_hash_capacity - 1;
        uint32_t slot = chunk_hash_key(cx, cz) & (uint32_t)mask;
        for (int probes = 0; probes < w->chunk_hash_capacity; probes++) {
            Chunk *c = w->chunk_hash[slot];
            if (!c)
                break;
            if (c->cx == cx && c->cz == cz)
                return c;
            slot = (slot + 1) & (uint32_t)mask;
        }
    }
    if (!create)
        return NULL;
    Chunk *c = chunk_new(cx, cz);
    world_add_chunk(w, c);
    return c;
}

Chunk *world_get_chunk(World *w, int cx, int cz, bool create) {
    return world_find_chunk(w, cx, cz, create);
}

uint8_t world_get_block(World *w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return BLOCK_AIR;
    int cx = floor_div16(x);
    int cz = floor_div16(z);
    Chunk *c = world_find_chunk(w, cx, cz, false);
    if (!c || !c->generated)
        return BLOCK_AIR;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return c->blocks[chunk_index_local(lx, y, lz)];
}

static void chunk_recompute_height(Chunk *c, int lx, int lz) {
    int top = 0;
    for (int y = WORLD_HEIGHT - 1; y >= 0; y--) {
        uint8_t b = c->blocks[chunk_index_local(lx, y, lz)];
        if (b != BLOCK_AIR && b != BLOCK_WATER && b != BLOCK_LAVA) {
            top = y;
            break;
        }
    }
    c->heightmap[lx + lz * CHUNK_SIZE] = (uint8_t)top;
}

static void world_mark_neighbors(World *w, Chunk *c, int lx, int lz) {
    c->dirty = true;
    if (lx == 0)
        world_find_chunk(w, c->cx - 1, c->cz, false) ? (world_find_chunk(w, c->cx - 1, c->cz, false)->dirty = true) : 0;
    if (lx == 15)
        world_find_chunk(w, c->cx + 1, c->cz, false) ? (world_find_chunk(w, c->cx + 1, c->cz, false)->dirty = true) : 0;
    if (lz == 0)
        world_find_chunk(w, c->cx, c->cz - 1, false) ? (world_find_chunk(w, c->cx, c->cz - 1, false)->dirty = true) : 0;
    if (lz == 15)
        world_find_chunk(w, c->cx, c->cz + 1, false) ? (world_find_chunk(w, c->cx, c->cz + 1, false)->dirty = true) : 0;
}

void world_set_block(World *w, int x, int y, int z, uint8_t block, bool record) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return;
    int cx = floor_div16(x);
    int cz = floor_div16(z);
    Chunk *c = world_get_chunk(w, cx, cz, true);
    if (!c->generated)
        world_generate_chunk_for_edit(w, c);
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    int idx = chunk_index_local(lx, y, lz);
    if (c->blocks[idx] == block)
        return;
    c->blocks[idx] = block;
    chunk_recompute_height(c, lx, lz);
    world_mark_neighbors(w, c, lx, lz);

    if (block == BLOCK_WATER || block == BLOCK_LAVA || block == BLOCK_AIR) {
        world_schedule_liquid_tick(w, x, y, z);
        world_schedule_liquid_tick(w, x + 1, y, z);
        world_schedule_liquid_tick(w, x - 1, y, z);
        world_schedule_liquid_tick(w, x, y + 1, z);
        world_schedule_liquid_tick(w, x, y - 1, z);
        world_schedule_liquid_tick(w, x, y, z + 1);
        world_schedule_liquid_tick(w, x, y, z - 1);
    }

    if (block == BLOCK_REDSTONE_WIRE || block == BLOCK_REDSTONE_TORCH ||
        block == BLOCK_REDSTONE_BLOCK || block == BLOCK_LEVER || block == BLOCK_STONE_BUTTON ||
        block == BLOCK_REDSTONE_LAMP_OFF || block == BLOCK_REDSTONE_LAMP_ON) {
        world_update_redstone_at(w, x, y, z);
        world_update_redstone_at(w, x + 1, y, z);
        world_update_redstone_at(w, x - 1, y, z);
        world_update_redstone_at(w, x, y + 1, z);
        world_update_redstone_at(w, x, y - 1, z);
        world_update_redstone_at(w, x, y, z + 1);
        world_update_redstone_at(w, x, y, z - 1);
    } else if (block == BLOCK_AIR) {
        world_set_power(w, x, y, z, 0);
        world_update_redstone_at(w, x + 1, y, z);
        world_update_redstone_at(w, x - 1, y, z);
        world_update_redstone_at(w, x, y + 1, z);
        world_update_redstone_at(w, x, y - 1, z);
        world_update_redstone_at(w, x, y, z + 1);
        world_update_redstone_at(w, x, y, z - 1);
    }

    if (record) {
        bool replaced = false;
        for (int i = 0; i < w->mod_count; i++) {
            if (w->mods[i].x == x && w->mods[i].y == y && w->mods[i].z == z) {
                w->mods[i].block = block;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            if (w->mod_count == w->mod_capacity) {
                w->mod_capacity = w->mod_capacity ? w->mod_capacity * 2 : 256;
                w->mods = xrealloc(w->mods, (size_t)w->mod_capacity * sizeof(BlockMod));
            }
            BlockMod *m = &w->mods[w->mod_count++];
            m->x = x;
            m->y = (int16_t)y;
            m->z = z;
            m->block = block;
        }
        w->dirty = true;
    }
}

void world_set_block_raw(World *w, int x, int y, int z, uint8_t block) {
    world_set_block(w, x, y, z, block, false);
}

int world_height_at(World *w, int x, int z) {
    int cx = floor_div16(x);
    int cz = floor_div16(z);
    Chunk *c = world_find_chunk(w, cx, cz, false);
    if (!c || !c->generated)
        return SEA_LEVEL;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return c->heightmap[lx + lz * CHUNK_SIZE];
}

typedef enum {
    BIOME_OCEAN,
    BIOME_BEACH,
    BIOME_PLAINS,
    BIOME_FOREST,
    BIOME_DESERT,
    BIOME_SNOWY,
    BIOME_MOUNTAIN
} Biome;

static int terrain_height(int x, int z, uint64_t seed) {
    float c = fbm2(x * 0.0030f, z * 0.0030f, seed ^ 0xA1B2C3D4ULL, 4);
    float r = ridged2(x * 0.010f, z * 0.010f, seed ^ 0x51ED270BULL, 4);
    float d = fbm2(x * 0.055f, z * 0.055f, seed ^ 0x7F4A7C15ULL, 2);
    float h = 54.0f + c * 19.0f + (r - 0.43f) * 30.0f + d * 3.0f;
    if (h < 5.0f)
        h = 5.0f;
    if (h > 118.0f)
        h = 118.0f;
    return (int)floorf(h);
}

static Biome biome_at(int x, int z, int h, uint64_t seed) {
    if (h < SEA_LEVEL - 1)
        return BIOME_OCEAN;
    if (h <= SEA_LEVEL + 2)
        return BIOME_BEACH;
    if (h > 82)
        return BIOME_MOUNTAIN;
    float temp = fbm2(x * 0.0018f, z * 0.0018f, seed ^ 0xDEADBEEFULL, 3);
    float hum = fbm2(x * 0.0024f, z * 0.0024f, seed ^ 0xCAFEBABEULL, 3);
    if (temp < -0.28f)
        return BIOME_SNOWY;
    if (temp > 0.22f && hum < 0.03f)
        return BIOME_DESERT;
    if (hum > 0.10f)
        return BIOME_FOREST;
    return BIOME_PLAINS;
}

static void set_local(Chunk *c, int x, int y, int z, uint8_t b) {
    if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || y < 0 || y >= WORLD_HEIGHT)
        return;
    c->blocks[chunk_index_local(x, y, z)] = b;
}

static uint8_t get_local(Chunk *c, int x, int y, int z) {
    if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || y < 0 || y >= WORLD_HEIGHT)
        return BLOCK_AIR;
    return c->blocks[chunk_index_local(x, y, z)];
}

static void place_tree(World *w, Chunk *c, int lx, int lz, int h, uint64_t seed) {
    uint64_t rnd = (uint64_t)mced_hash_u32((uint32_t)(c->cx * CHUNK_SIZE + lx) * 374761393U ^
                                          (uint32_t)(c->cz * CHUNK_SIZE + lz) * 668265263U ^
                                          (uint32_t)seed);
    int height = 4 + (int)(rnd % 3);
    BlockId log = BLOCK_OAK_LOG;
    BlockId leaves = BLOCK_OAK_LEAVES;

    float temp = fbm2((c->cx * CHUNK_SIZE + lx) * 0.0018f,
                      (c->cz * CHUNK_SIZE + lz) * 0.0018f, seed ^ 0xDEADBEEFULL, 3);
    float hum = fbm2((c->cx * CHUNK_SIZE + lx) * 0.0024f,
                     (c->cz * CHUNK_SIZE + lz) * 0.0024f, seed ^ 0xCAFEBABEULL, 3);
    if (temp < -0.28f) {
        log = BLOCK_SPRUCE_LOG;
        leaves = BLOCK_SPRUCE_LEAVES;
        height = 5 + (int)(rnd % 3);
    } else if (hum > 0.28f) {
        log = BLOCK_BIRCH_LOG;
        leaves = BLOCK_BIRCH_LEAVES;
    } else if (hum > 0.16f) {
        log = BLOCK_JUNGLE_LOG;
        leaves = BLOCK_JUNGLE_LEAVES;
        height = 5 + (int)(rnd % 4);
    }

    for (int dy = 1; dy <= height; dy++)
        set_local(c, lx, h + dy, lz, log);

    int top = h + height;
    for (int dy = -2; dy <= 1; dy++) {
        int radius = (dy <= -1) ? 2 : 1;
        for (int dx = -radius; dx <= radius; dx++) {
            for (int dz = -radius; dz <= radius; dz++) {
                if (dx == 0 && dz == 0 && dy <= 0)
                    continue;
                if (abs(dx) == radius && abs(dz) == radius && (dy == 1 || (dy == 0 && (rnd & 1))))
                    continue;
                if (get_local(c, lx + dx, top + dy, lz + dz) == BLOCK_AIR)
                    set_local(c, lx + dx, top + dy, lz + dz, leaves);
            }
        }
    }
}

void world_generate_chunk_for_edit(World *w, Chunk *c) {
    if (c->generated)
        return;
    memset(c->blocks, 0, sizeof(c->blocks));

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = terrain_height(wx, wz, w->seed);
            Biome biome = biome_at(wx, wz, h, w->seed);

            uint8_t top = BLOCK_GRASS_BLOCK;
            uint8_t filler = BLOCK_DIRT;
            uint8_t deep = BLOCK_STONE;

            if (biome == BIOME_OCEAN || biome == BIOME_BEACH || biome == BIOME_DESERT) {
                top = BLOCK_SAND;
                filler = BLOCK_SAND;
            } else if (biome == BIOME_SNOWY) {
                top = BLOCK_SNOW_BLOCK;
                filler = BLOCK_DIRT;
            } else if (biome == BIOME_MOUNTAIN) {
                top = (h > 91) ? BLOCK_SNOW_BLOCK : BLOCK_STONE;
                filler = BLOCK_STONE;
            }

            for (int y = 0; y <= h; y++) {
                uint8_t b;
                if (y == 0)
                    b = BLOCK_BEDROCK;
                else if (y <= 2 && (mced_hash_u32((uint32_t)(wx * 31 + wz * 17 + y) ^ (uint32_t)w->seed) & 3) == 0)
                    b = BLOCK_BEDROCK;
                else if (y == h)
                    b = top;
                else if (y >= h - 3)
                    b = filler;
                else
                    b = (y < 18) ? BLOCK_DEEPSLATE : deep;
                c->blocks[chunk_index_local(lx, y, lz)] = b;
            }

            if (biome == BIOME_OCEAN && h < SEA_LEVEL - 4) {
                uint32_t gravel = mced_hash_u32((uint32_t)wx * 13U ^ (uint32_t)wz * 31U ^ (uint32_t)w->seed);
                if ((gravel & 15) == 0)
                    c->blocks[chunk_index_local(lx, h, lz)] = BLOCK_GRAVEL;
                else if ((gravel & 31) == 0)
                    c->blocks[chunk_index_local(lx, h, lz)] = BLOCK_CLAY;
            }

            for (int y = h + 1; y <= SEA_LEVEL; y++)
                c->blocks[chunk_index_local(lx, y, lz)] = BLOCK_WATER;

            c->heightmap[lx + lz * CHUNK_SIZE] = (uint8_t)h;
        }
    }

    /* Cave tunnels: two narrow 3D noise fields.  Avoid draining oceans and
       avoid cutting the top three blocks of the surface. */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = c->heightmap[lx + lz * CHUNK_SIZE];
            if (h <= SEA_LEVEL + 2)
                continue;
            for (int y = 4; y < h - 3; y++) {
                uint8_t b = c->blocks[chunk_index_local(lx, y, lz)];
                if (b == BLOCK_AIR || b == BLOCK_BEDROCK || b == BLOCK_WATER)
                    continue;
                float n1 = noise3(wx * 0.045f, y * 0.060f, wz * 0.045f, w->seed ^ 0xC0FFEEULL);
                if (fabsf(n1) < 0.065f) {
                    float n2 = noise3(wx * 0.045f, y * 0.060f, wz * 0.045f, w->seed ^ 0xBADC0DEULL);
                    if (fabsf(n2) < 0.075f) {
                        c->blocks[chunk_index_local(lx, y, lz)] = BLOCK_AIR;
                        continue;
                    }
                }
                if (y < 45) {
                    float cavern = fbm3(wx * 0.018f, y * 0.022f, wz * 0.018f, w->seed ^ 0x1234ABCDULL, 2);
                    if (cavern > 0.56f)
                        c->blocks[chunk_index_local(lx, y, lz)] = BLOCK_AIR;
                }
            }
        }
    }

    /* Ore veins.  These are intentionally chunky and common enough to find. */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = c->heightmap[lx + lz * CHUNK_SIZE];
            for (int y = 2; y <= h; y++) {
                int idx = chunk_index_local(lx, y, lz);
                uint8_t b = c->blocks[idx];
                if (b != BLOCK_STONE && b != BLOCK_DEEPSLATE)
                    continue;
                uint32_t r = mced_hash_u32((uint32_t)wx * 0x9e3779b1U ^
                                           (uint32_t)wz * 0x85ebca6bU ^
                                           (uint32_t)y * 0xc2b2ae35U ^
                                           (uint32_t)w->seed);
                uint32_t roll = r % 10000U;
                if (y < 16 && roll < 12)
                    c->blocks[idx] = BLOCK_DIAMOND_ORE;
                else if (y < 24 && roll < 42)
                    c->blocks[idx] = BLOCK_REDSTONE_ORE;
                else if (y < 32 && roll < 62)
                    c->blocks[idx] = BLOCK_LAPIS_ORE;
                else if (y < 32 && roll < 90)
                    c->blocks[idx] = BLOCK_GOLD_ORE;
                else if (y < 24 && roll < 120)
                    c->blocks[idx] = BLOCK_EMERALD_ORE;
                else if (y < 64 && roll < 220)
                    c->blocks[idx] = BLOCK_IRON_ORE;
                else if (y < 64 && roll < 340)
                    c->blocks[idx] = BLOCK_COPPER_ORE;
                else if (y < 90 && roll < 500)
                    c->blocks[idx] = BLOCK_COAL_ORE;
            }
        }
    }

    /* Vegetation is placed with a 2-block margin, so it never needs to write
       into a neighbouring chunk. */
    for (int lx = 2; lx < CHUNK_SIZE - 2; lx++) {
        for (int lz = 2; lz < CHUNK_SIZE - 2; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = c->heightmap[lx + lz * CHUNK_SIZE];
            if (h <= SEA_LEVEL + 1)
                continue;
            uint8_t top = c->blocks[chunk_index_local(lx, h, lz)];
            Biome biome = biome_at(wx, wz, h, w->seed);
            uint32_t r = mced_hash_u32((uint32_t)wx * 0x27d4eb2dU ^
                                       (uint32_t)wz * 0x165667b1U ^
                                       (uint32_t)w->seed ^ 0xA57E1234U);
            if (top == BLOCK_GRASS_BLOCK) {
                int chance = (biome == BIOME_FOREST) ? 36 : (biome == BIOME_PLAINS ? 170 : 0);
                if (chance && (int)(r % (uint32_t)chance) == 0) {
                    place_tree(w, c, lx, lz, h, w->seed);
                } else if ((r & 0x3ff) == 0) {
                    set_local(c, lx, h + 1, lz, BLOCK_PUMPKIN);
                } else if ((r % 100U) < 22) {
                    uint32_t flor = (r / 100U) % 100U;
                    if (flor < 75)
                        set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                    else if (flor < 88)
                        set_local(c, lx, h + 1, lz, BLOCK_DANDELION);
                    else
                        set_local(c, lx, h + 1, lz, BLOCK_POPPY);
                }
            } else if (top == BLOCK_SAND && biome == BIOME_DESERT) {
                if ((r % 140U) == 0) {
                    int height = 2 + (int)(r % 3U);
                    for (int y = 1; y <= height; y++)
                        set_local(c, lx, h + y, lz, BLOCK_CACTUS);
                } else if ((r % 90U) == 1) {
                    set_local(c, lx, h + 1, lz, BLOCK_DEAD_BUSH);
                }
            }

            /* Sugar cane along water edges */
            if ((top == BLOCK_SAND || top == BLOCK_GRASS_BLOCK || top == BLOCK_DIRT) && h == SEA_LEVEL + 1) {
                bool near_water = false;
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dz = -1; dz <= 1; dz++) {
                        if (abs(dx) + abs(dz) != 1) continue;
                        int nlx = lx + dx, nlz = lz + dz;
                        if (nlx >= 0 && nlx < CHUNK_SIZE && nlz >= 0 && nlz < CHUNK_SIZE) {
                            if (c->blocks[chunk_index_local(nlx, SEA_LEVEL, nlz)] == BLOCK_WATER) {
                                near_water = true;
                                break;
                            }
                        }
                    }
                    if (near_water) break;
                }
                if (near_water && (r % 14U) == 0) {
                    int sc_h = 2 + (int)(r % 2U);
                    for (int y = 1; y <= sc_h; y++)
                        set_local(c, lx, h + y, lz, BLOCK_SUGAR_CANE);
                }
            }
        }
    }

    /* Apply player edits after terrain generation. */
    for (int i = 0; i < w->mod_count; i++) {
        BlockMod *m = &w->mods[i];
        if (floor_div16(m->x) != c->cx || floor_div16(m->z) != c->cz)
            continue;
        int lx = m->x - c->cx * CHUNK_SIZE;
        int lz = m->z - c->cz * CHUNK_SIZE;
        if (m->y >= 0 && m->y < WORLD_HEIGHT)
            c->blocks[chunk_index_local(lx, m->y, lz)] = m->block;
    }

    c->generated = true;
    c->dirty = true;

    /* Existing neighbours now have a real border to hide faces against. */
    Chunk *n;
    if ((n = world_find_chunk(w, c->cx - 1, c->cz, false))) n->dirty = true;
    if ((n = world_find_chunk(w, c->cx + 1, c->cz, false))) n->dirty = true;
    if ((n = world_find_chunk(w, c->cx, c->cz - 1, false))) n->dirty = true;
    if ((n = world_find_chunk(w, c->cx, c->cz + 1, false))) n->dirty = true;
}

static void world_generate_chunk(World *w, Chunk *c) {
    world_generate_chunk_for_edit(w, c);
}

void world_preload(World *w, double player_x, double player_z, int radius) {
    int pcx = floor_div16((int)floor(player_x));
    int pcz = floor_div16((int)floor(player_z));
    for (int r = 0; r <= radius; r++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                if (r > 0 && abs(dx) != r && abs(dz) != r)
                    continue;
                Chunk *c = world_find_chunk(w, pcx + dx, pcz + dz, true);
                if (!c->generated)
                    world_generate_chunk(w, c);
            }
        }
    }
}

void world_update(World *w, double player_x, double player_z) {
    world_tick_liquids(w);

    int pcx = floor_div16((int)floor(player_x));
    int pcz = floor_div16((int)floor(player_z));
    if (pcx != w->last_pcx || pcz != w->last_pcz) {
        w->last_pcx = pcx;
        w->last_pcz = pcz;
        w->fully_loaded = false;
    }
    if (w->fully_loaded)
        return;

    int radius = w->load_radius + 1;
    int budget = 3;
    bool any_missing = false;
    for (int r = 0; r <= radius && budget > 0; r++) {
        for (int dz = -r; dz <= r && budget > 0; dz++) {
            for (int dx = -r; dx <= r && budget > 0; dx++) {
                if (r > 0 && abs(dx) != r && abs(dz) != r)
                    continue;
                Chunk *c = world_find_chunk(w, pcx + dx, pcz + dz, false);
                if (!c) {
                    any_missing = true;
                    c = world_find_chunk(w, pcx + dx, pcz + dz, true);
                    world_generate_chunk(w, c);
                    budget--;
                }
            }
        }
    }
    if (!any_missing && budget == 3) {
        w->fully_loaded = true;
    }
    world_tick_liquids(w);
}

void world_schedule_liquid_tick(World *w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return;
    int next_tail = (w->liquid_queue_tail + 1) % MAX_LIQUID_TICKS;
    if (next_tail == w->liquid_queue_head)
        return;
    w->liquid_queue[w->liquid_queue_tail] = (LiquidCoord){ (int32_t)x, (int16_t)y, (int32_t)z };
    w->liquid_queue_tail = next_tail;
}

void world_tick_liquids(World *w) {
    int budget = 64;
    static const int hdx[4] = {1, -1, 0, 0};
    static const int hdz[4] = {0, 0, 1, -1};

    while (w->liquid_queue_head != w->liquid_queue_tail && budget-- > 0) {
        LiquidCoord coord = w->liquid_queue[w->liquid_queue_head];
        w->liquid_queue_head = (w->liquid_queue_head + 1) % MAX_LIQUID_TICKS;

        int x = coord.x;
        int y = coord.y;
        int z = coord.z;
        if (y < 0 || y >= WORLD_HEIGHT)
            continue;

        uint8_t cur = world_get_block(w, x, y, z);

        /* 1. Infinite water source regeneration (Air block with >= 2 water neighbors and solid bottom) */
        if (cur == BLOCK_AIR) {
            uint8_t under = (y > 0) ? world_get_block(w, x, y - 1, z) : BLOCK_BEDROCK;
            if (under != BLOCK_AIR) {
                int water_sources = 0;
                for (int i = 0; i < 4; i++) {
                    if (world_get_block(w, x + hdx[i], y, z + hdz[i]) == BLOCK_WATER)
                        water_sources++;
                }
                if (water_sources >= 2) {
                    world_set_block(w, x, y, z, BLOCK_WATER, true);
                    cur = BLOCK_WATER;
                    for (int i = 0; i < 4; i++)
                        world_schedule_liquid_tick(w, x + hdx[i], y, z + hdz[i]);
                    world_schedule_liquid_tick(w, x, y - 1, z);
                }
            }
        }

        /* 2. Water flowing downwards and spreading */
        if (cur == BLOCK_WATER) {
            if (y > 0) {
                uint8_t below = world_get_block(w, x, y - 1, z);
                if (below == BLOCK_AIR) {
                    world_set_block(w, x, y - 1, z, BLOCK_WATER, true);
                    world_schedule_liquid_tick(w, x, y - 1, z);
                    continue;
                } else if (below == BLOCK_LAVA) {
                    world_set_block(w, x, y - 1, z, BLOCK_OBSIDIAN, true);
                    continue;
                }
            }
            uint8_t under = (y > 0) ? world_get_block(w, x, y - 1, z) : BLOCK_BEDROCK;
            if (under != BLOCK_AIR) {
                for (int i = 0; i < 4; i++) {
                    int nx = x + hdx[i];
                    int nz = z + hdz[i];
                    uint8_t nb = world_get_block(w, nx, y, nz);
                    if (nb == BLOCK_LAVA) {
                        world_set_block(w, nx, y, nz, BLOCK_COBBLESTONE, true);
                    } else if (nb == BLOCK_AIR) {
                        world_set_block(w, nx, y, nz, BLOCK_WATER, true);
                        world_schedule_liquid_tick(w, nx, y, nz);
                    }
                }
            }
        }

        /* 3. Lava flowing downwards and spreading */
        else if (cur == BLOCK_LAVA) {
            if (y > 0) {
                uint8_t below = world_get_block(w, x, y - 1, z);
                if (below == BLOCK_AIR) {
                    world_set_block(w, x, y - 1, z, BLOCK_LAVA, true);
                    world_schedule_liquid_tick(w, x, y - 1, z);
                    continue;
                } else if (below == BLOCK_WATER) {
                    world_set_block(w, x, y - 1, z, BLOCK_STONE, true);
                    continue;
                }
            }
            uint8_t under = (y > 0) ? world_get_block(w, x, y - 1, z) : BLOCK_BEDROCK;
            if (under != BLOCK_AIR) {
                for (int i = 0; i < 4; i++) {
                    int nx = x + hdx[i];
                    int nz = z + hdz[i];
                    uint8_t nb = world_get_block(w, nx, y, nz);
                    if (nb == BLOCK_WATER) {
                        world_set_block(w, nx, y, nz, BLOCK_COBBLESTONE, true);
                    } else if (nb == BLOCK_AIR) {
                        world_set_block(w, nx, y, nz, BLOCK_LAVA, true);
                        world_schedule_liquid_tick(w, nx, y, nz);
                    }
                }
            }
        }
    }
}

uint8_t world_get_power(World *w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return 0;
    int cx = floor_div16(x);
    int cz = floor_div16(z);
    Chunk *c = world_find_chunk(w, cx, cz, false);
    if (!c || !c->generated)
        return 0;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return c->redstone_power[chunk_index_local(lx, y, lz)];
}

void world_set_power(World *w, int x, int y, int z, uint8_t power) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return;
    int cx = floor_div16(x);
    int cz = floor_div16(z);
    Chunk *c = world_get_chunk(w, cx, cz, true);
    if (!c || !c->generated)
        return;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    int idx = chunk_index_local(lx, y, lz);
    if (c->redstone_power[idx] == power)
        return;
    c->redstone_power[idx] = power;
    c->dirty = true;
}

void world_update_redstone_at(World *w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return;
    uint8_t block = world_get_block(w, x, y, z);
    static const int hdx[4] = {1, -1, 0, 0};
    static const int hdz[4] = {0, 0, 1, -1};
    static const int fdx6[6] = {1, -1, 0, 0, 0, 0};
    static const int fdy6[6] = {0, 0, 1, -1, 0, 0};
    static const int fdz6[6] = {0, 0, 0, 0, 1, -1};

    /* Redstone Torch emits power 15 */
    if (block == BLOCK_REDSTONE_TORCH || block == BLOCK_REDSTONE_BLOCK) {
        world_set_power(w, x, y, z, 15);
        for (int i = 0; i < 6; i++) {
            int nx = x + fdx6[i], ny = y + fdy6[i], nz = z + fdz6[i];
            uint8_t nb = world_get_block(w, nx, ny, nz);
            if (nb == BLOCK_REDSTONE_WIRE)
                world_update_redstone_at(w, nx, ny, nz);
            else if (nb == BLOCK_REDSTONE_LAMP_OFF)
                world_set_block(w, nx, ny, nz, BLOCK_REDSTONE_LAMP_ON, true);
            else if (nb == BLOCK_OAK_DOOR)
                world_set_block(w, nx, ny, nz, BLOCK_OAK_DOOR_OPEN, true);
            else if (nb == BLOCK_IRON_DOOR)
                world_set_block(w, nx, ny, nz, BLOCK_IRON_DOOR_OPEN, true);
            else if (nb == BLOCK_OAK_TRAPDOOR)
                world_set_block(w, nx, ny, nz, BLOCK_OAK_TRAPDOOR_OPEN, true);
        }
        return;
    }

    /* Lever or Button */
    if (block == BLOCK_LEVER || block == BLOCK_STONE_BUTTON) {
        uint8_t p = world_get_power(w, x, y, z);
        for (int i = 0; i < 6; i++) {
            int nx = x + fdx6[i], ny = y + fdy6[i], nz = z + fdz6[i];
            uint8_t nb = world_get_block(w, nx, ny, nz);
            if (nb == BLOCK_REDSTONE_WIRE)
                world_update_redstone_at(w, nx, ny, nz);
            else if (nb == BLOCK_REDSTONE_LAMP_OFF && p > 0)
                world_set_block(w, nx, ny, nz, BLOCK_REDSTONE_LAMP_ON, true);
            else if (nb == BLOCK_REDSTONE_LAMP_ON && p == 0)
                world_set_block(w, nx, ny, nz, BLOCK_REDSTONE_LAMP_OFF, true);
            else if (nb == BLOCK_OAK_DOOR && p > 0)
                world_set_block(w, nx, ny, nz, BLOCK_OAK_DOOR_OPEN, true);
            else if (nb == BLOCK_OAK_DOOR_OPEN && p == 0)
                world_set_block(w, nx, ny, nz, BLOCK_OAK_DOOR, true);
            else if (nb == BLOCK_IRON_DOOR && p > 0)
                world_set_block(w, nx, ny, nz, BLOCK_IRON_DOOR_OPEN, true);
            else if (nb == BLOCK_IRON_DOOR_OPEN && p == 0)
                world_set_block(w, nx, ny, nz, BLOCK_IRON_DOOR, true);
            else if (nb == BLOCK_OAK_TRAPDOOR && p > 0)
                world_set_block(w, nx, ny, nz, BLOCK_OAK_TRAPDOOR_OPEN, true);
            else if (nb == BLOCK_OAK_TRAPDOOR_OPEN && p == 0)
                world_set_block(w, nx, ny, nz, BLOCK_OAK_TRAPDOOR, true);
        }
        return;
    }

    /* Redstone Wire power calculation */
    if (block == BLOCK_REDSTONE_WIRE) {
        uint8_t max_p = 0;
        for (int i = 0; i < 6; i++) {
            int nx = x + fdx6[i], ny = y + fdy6[i], nz = z + fdz6[i];
            uint8_t nb = world_get_block(w, nx, ny, nz);
            if (nb == BLOCK_REDSTONE_TORCH || nb == BLOCK_REDSTONE_BLOCK) {
                max_p = 15;
                break;
            }
            if (nb == BLOCK_LEVER && world_get_power(w, nx, ny, nz) == 15) {
                max_p = 15;
                break;
            }
        }
        if (max_p < 15) {
            for (int i = 0; i < 4; i++) {
                int nx = x + hdx[i], nz = z + hdz[i];
                uint8_t nb = world_get_block(w, nx, y, nz);
                if (nb == BLOCK_REDSTONE_WIRE) {
                    uint8_t np = world_get_power(w, nx, y, nz);
                    if (np > 0 && np - 1 > max_p)
                        max_p = np - 1;
                }
            }
        }
        uint8_t old_p = world_get_power(w, x, y, z);
        if (old_p != max_p) {
            world_set_power(w, x, y, z, max_p);
            for (int i = 0; i < 4; i++) {
                int nx = x + hdx[i], nz = z + hdz[i];
                uint8_t nb = world_get_block(w, nx, y, nz);
                if (nb == BLOCK_REDSTONE_WIRE)
                    world_update_redstone_at(w, nx, y, nz);
                else if (nb == BLOCK_REDSTONE_LAMP_OFF && max_p > 0)
                    world_set_block(w, nx, y, nz, BLOCK_REDSTONE_LAMP_ON, true);
                else if (nb == BLOCK_REDSTONE_LAMP_ON && max_p == 0)
                    world_set_block(w, nx, y, nz, BLOCK_REDSTONE_LAMP_OFF, true);
                else if (nb == BLOCK_OAK_DOOR && max_p > 0)
                    world_set_block(w, nx, y, nz, BLOCK_OAK_DOOR_OPEN, true);
                else if (nb == BLOCK_OAK_DOOR_OPEN && max_p == 0)
                    world_set_block(w, nx, y, nz, BLOCK_OAK_DOOR, true);
                else if (nb == BLOCK_IRON_DOOR && max_p > 0)
                    world_set_block(w, nx, y, nz, BLOCK_IRON_DOOR_OPEN, true);
                else if (nb == BLOCK_IRON_DOOR_OPEN && max_p == 0)
                    world_set_block(w, nx, y, nz, BLOCK_IRON_DOOR, true);
                else if (nb == BLOCK_OAK_TRAPDOOR && max_p > 0)
                    world_set_block(w, nx, y, nz, BLOCK_OAK_TRAPDOOR_OPEN, true);
                else if (nb == BLOCK_OAK_TRAPDOOR_OPEN && max_p == 0)
                    world_set_block(w, nx, y, nz, BLOCK_OAK_TRAPDOOR, true);
            }
        }
        return;
    }
}

bool world_find_spawn(World *w, int *out_x, int *out_y, int *out_z) {
    world_preload(w, 0, 0, 2);
    for (int r = 0; r < 96; r++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                if (r > 0 && abs(dx) != r && abs(dz) != r)
                    continue;
                int x = dx * 4;
                int z = dz * 4;
                world_preload(w, x, z, 1);
                int h = world_height_at(w, x, z);
                if (h <= SEA_LEVEL + 1)
                    continue;
                uint8_t ground = world_get_block(w, x, h, z);
                uint8_t above = world_get_block(w, x, h + 1, z);
                uint8_t above2 = world_get_block(w, x, h + 2, z);
                if (ground != BLOCK_WATER && ground != BLOCK_LAVA && above == BLOCK_AIR && above2 == BLOCK_AIR) {
                    *out_x = x;
                    *out_y = h + 1;
                    *out_z = z;
                    return true;
                }
            }
        }
    }
    *out_x = 0;
    *out_y = SEA_LEVEL + 4;
    *out_z = 0;
    return false;
}

bool world_write(World *w, FILE *f) {
    if (!f)
        return false;
    if (fwrite(WORLD_MAGIC, 1, 4, f) != 4)
        return false;
    uint32_t version = 1;
    if (fwrite(&version, sizeof(version), 1, f) != 1)
        return false;
    if (fwrite(&w->seed, sizeof(w->seed), 1, f) != 1)
        return false;
    int32_t count = w->mod_count;
    if (fwrite(&count, sizeof(count), 1, f) != 1)
        return false;
    if (count > 0 && fwrite(w->mods, sizeof(BlockMod), (size_t)count, f) != (size_t)count)
        return false;
    return true;
}

bool world_read(World *w, FILE *f) {
    if (!f)
        return false;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, WORLD_MAGIC, 4) != 0)
        return false;
    uint32_t version = 0;
    if (fread(&version, sizeof(version), 1, f) != 1 || version != 1)
        return false;
    uint64_t seed = 0;
    if (fread(&seed, sizeof(seed), 1, f) != 1 || seed != w->seed)
        return false;
    int32_t count = 0;
    if (fread(&count, sizeof(count), 1, f) != 1 || count < 0 || count > 10000000)
        return false;
    for (int i = 0; i < count; i++) {
        BlockMod m;
        if (fread(&m, sizeof(m), 1, f) != 1)
            return false;
        world_set_block(w, m.x, m.y, m.z, m.block, false);
        if (w->mod_count == w->mod_capacity) {
            w->mod_capacity = w->mod_capacity ? w->mod_capacity * 2 : 256;
            w->mods = xrealloc(w->mods, (size_t)w->mod_capacity * sizeof(BlockMod));
        }
        w->mods[w->mod_count++] = m;
    }
    w->dirty = false;
    return true;
}

bool world_probe_seed(const char *path, uint64_t *seed, bool *has_save) {
    *has_save = false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char magic[4];
    uint32_t version;
    uint64_t s;
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, WORLD_MAGIC, 4) == 0 &&
              fread(&version, sizeof(version), 1, f) == 1 && version == 1 &&
              fread(&s, sizeof(s), 1, f) == 1;
    fclose(f);
    if (ok) {
        *seed = s;
        *has_save = true;
    }
    return ok;
}

#include "world.h"

#include "mced.h"
#include "noise.h"
#include "thread_pool.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>

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

    pthread_mutex_t *mtx = malloc(sizeof(pthread_mutex_t));
    if (mtx) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(mtx, &attr);
        pthread_mutexattr_destroy(&attr);
    }
    w->chunk_mutex = mtx;
}

void world_destroy(World *w) {
    if (w->pool) {
        thread_pool_wait_idle(w->pool);
    }
    if (w->chunk_mutex) {
        pthread_mutex_t *mtx = (pthread_mutex_t *)w->chunk_mutex;
        pthread_mutex_destroy(mtx);
        free(mtx);
        w->chunk_mutex = NULL;
    }
    for (int i = 0; i < w->chunk_count; i++)
        free(w->chunks[i]);
    free(w->chunks);
    free(w->chunk_hash);
    free(w->mods);
    memset(w, 0, sizeof(*w));
}

void world_set_thread_pool(World *w, struct ThreadPool *pool) {
    w->pool = pool;
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
    if (w->chunk_mutex) pthread_mutex_lock((pthread_mutex_t *)w->chunk_mutex);
    Chunk *res = NULL;
    if (w->chunk_hash_capacity > 0) {
        int mask = w->chunk_hash_capacity - 1;
        uint32_t slot = chunk_hash_key(cx, cz) & (uint32_t)mask;
        for (int probes = 0; probes < w->chunk_hash_capacity; probes++) {
            Chunk *c = w->chunk_hash[slot];
            if (!c)
                break;
            if (c->cx == cx && c->cz == cz) {
                res = c;
                break;
            }
            slot = (slot + 1) & (uint32_t)mask;
        }
    }
    if (!res && create) {
        Chunk *c = chunk_new(cx, cz);
        world_add_chunk(w, c);
        res = c;
    }
    if (w->chunk_mutex) pthread_mutex_unlock((pthread_mutex_t *)w->chunk_mutex);
    return res;
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
    c->heightmap[lx + lz * CHUNK_SIZE] = (uint16_t)top;
}

static void world_mark_neighbors(World *w, Chunk *c, int lx, int lz) {
    c->dirty = true;
    c->version++;
    Chunk *n;
    if (lx == 0 && (n = world_find_chunk(w, c->cx - 1, c->cz, false))) { n->dirty = true; n->version++; }
    if (lx == 15 && (n = world_find_chunk(w, c->cx + 1, c->cz, false))) { n->dirty = true; n->version++; }
    if (lz == 0 && (n = world_find_chunk(w, c->cx, c->cz - 1, false))) { n->dirty = true; n->version++; }
    if (lz == 15 && (n = world_find_chunk(w, c->cx, c->cz + 1, false))) { n->dirty = true; n->version++; }
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
    if (block != BLOCK_WATER && block != BLOCK_LAVA) {
        c->fluid_dist[idx] = 0;
    }
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
    BIOME_DEEP_OCEAN,
    BIOME_BEACH,
    BIOME_SNOWY_BEACH,
    BIOME_STONY_SHORE,
    BIOME_PLAINS,
    BIOME_FLOWER_FOREST,
    BIOME_SUNFLOWER_PLAINS,
    BIOME_DAPPLED_FOREST,
    BIOME_FOREST,
    BIOME_BIRCH_FOREST,
    BIOME_OLD_GROWTH_BIRCH_FOREST,
    BIOME_DARK_FOREST,
    BIOME_TAIGA,
    BIOME_OLD_GROWTH_PINE_TAIGA,
    BIOME_OLD_GROWTH_SPRUCE_TAIGA,
    BIOME_SNOWY_TAIGA,
    BIOME_SNOWY_PLAINS,
    BIOME_ICE_SPIKES,
    BIOME_DESERT,
    BIOME_SAVANNA,
    BIOME_SAVANNA_PLATEAU,
    BIOME_BADLANDS,
    BIOME_ERODED_BADLANDS,
    BIOME_WOODED_BADLANDS,
    BIOME_SWAMP,
    BIOME_MANGROVE_SWAMP,
    BIOME_JUNGLE,
    BIOME_SPARSE_JUNGLE,
    BIOME_BAMBOO_JUNGLE,
    BIOME_WINDSWEPT_HILLS,
    BIOME_WINDSWEPT_GRAVELLY_HILLS,
    BIOME_WINDSWEPT_FOREST,
    BIOME_WINDSWEPT_SAVANNA,
    BIOME_MEADOW,
    BIOME_CHERRY_GROVE,
    BIOME_GROVE,
    BIOME_SNOWY_SLOPES,
    BIOME_JAGGED_PEAKS,
    BIOME_FROZEN_PEAKS,
    BIOME_STONY_PEAKS,
    BIOME_PALE_GARDEN,
    BIOME_MUSHROOM_FIELDS,
    /* 26.3 Underground Biomes */
    BIOME_SULFUR_CAVES,
    BIOME_LUSH_CAVES,
    BIOME_DRIPSTONE_CAVES,
    BIOME_DEEP_DARK
} Biome;

static inline float get_continentalness(int x, int z, uint64_t seed) {
    return fbm2(x * 0.0012f, z * 0.0012f, seed ^ 0xA1B2C3D4ULL, 4);
}

static inline float get_erosion(int x, int z, uint64_t seed) {
    return fbm2(x * 0.0016f, z * 0.0016f, seed ^ 0x51ED270BULL, 4);
}

static inline float get_weirdness(int x, int z, uint64_t seed) {
    return fbm2(x * 0.0022f, z * 0.0022f, seed ^ 0x7F4A7C15ULL, 3);
}

static inline float get_temperature(int x, int z, uint64_t seed) {
    return fbm2(x * 0.0010f, z * 0.0010f, seed ^ 0xDEADBEEFULL, 3);
}

static inline float get_humidity(int x, int z, uint64_t seed) {
    return fbm2(x * 0.0012f, z * 0.0012f, seed ^ 0xCAFEBABEULL, 3);
}

/* Authentic Mojang peaks_and_valleys transfer function from 26.3 TerrainProvider.class */
static inline float peaks_and_valleys(float weirdness) {
    return -(fabsf(fabsf(weirdness) - 0.6666667f) - 0.33333334f) * 3.0f;
}

/* 1:1 C implementation of Mojang 26.3 TerrainProvider.mountainContinentalness */
static inline float mountain_continentalness(float ridge, float modulation, float allow_rivers_below) {
    float ridge_slope = 1.0f - (1.0f - modulation) * 0.5f;
    float ridge_intersect = 0.5f * (1.0f - modulation);
    float adjusted_ridge_height = (ridge + 1.17f) * 0.46082947f;
    float continentalness = adjusted_ridge_height * ridge_slope - ridge_intersect;
    if (ridge < allow_rivers_below) {
        return fmaxf(continentalness, -0.2222f);
    }
    return fmaxf(continentalness, 0.0f);
}

/* 1:1 C implementation of Mojang 26.3 TerrainProvider.ridgeSpline */
static inline float eval_ridge_spline(float ridges, float valley, float low, float mid, float high, float peaks) {
    if (ridges <= -1.0f) return valley;
    if (ridges <= -0.4f) return valley + ((ridges + 1.0f) / 0.6f) * (low - valley);
    if (ridges <= 0.0f)  return low + ((ridges + 0.4f) / 0.4f) * (mid - low);
    if (ridges <= 0.4f)  return mid + (ridges / 0.4f) * (high - mid);
    if (ridges <= 1.0f)  return high + ((ridges - 0.4f) / 0.6f) * (peaks - high);
    return peaks;
}

/* 1:1 C implementation of Mojang 26.3 TerrainProvider.buildMountainRidgeSplineWithPoints */
static inline float eval_mountain_ridge(float ridges, float modulation, bool saddle) {
    float min_point_cont = mountain_continentalness(-1.0f, modulation, -0.7f);
    float max_point_cont = mountain_continentalness(1.0f, modulation, -0.7f);
    if (saddle && ridges < 0.0f) {
        float min_c = fmaxf(0.2f, min_point_cont);
        float mid_c = 0.5f * (min_point_cont + max_point_cont);
        float t = ridges + 1.0f;
        return min_c + t * (mid_c - min_c);
    }
    if (ridges <= -1.0f) return min_point_cont;
    if (ridges >= 1.0f) return max_point_cont;
    float t = (ridges + 1.0f) * 0.5f;
    return min_point_cont + t * (max_point_cont - min_point_cont);
}

/* 1:1 C implementation of Mojang 26.3 TerrainProvider.buildErosionOffsetSpline */
static inline float eval_erosion_offset(float erosion, float ridges,
                                        float low_valley, float hill, float tall_hill,
                                        float mountain_factor, float plain, float swamp,
                                        bool include_extreme_hills, bool saddle) {
    float very_low_mtn = eval_mountain_ridge(ridges, 0.6f + mountain_factor * 0.9f, saddle);
    float low_mtn = eval_mountain_ridge(ridges, 0.6f + mountain_factor * 0.4f, saddle);
    float mtn = eval_mountain_ridge(ridges, mountain_factor, saddle);
    float wide_plateau = eval_ridge_spline(ridges, low_valley - 0.15f, 0.5f * mountain_factor, 0.5f * mountain_factor, 0.5f * mountain_factor, 0.6f * mountain_factor);
    float narrow_plateau = eval_ridge_spline(ridges, low_valley, plain * mountain_factor, hill * mountain_factor, 0.5f * mountain_factor, 0.6f * mountain_factor);
    float plains = eval_ridge_spline(ridges, low_valley, plain, plain, hill, tall_hill);
    float swamps = eval_ridge_spline(ridges, -0.02f, swamp, swamp, hill, tall_hill);

    if (erosion <= -0.85f) return very_low_mtn;
    if (erosion <= -0.70f) {
        float t = (erosion + 0.85f) / 0.15f;
        return very_low_mtn + t * (low_mtn - very_low_mtn);
    }
    if (erosion <= -0.40f) {
        float t = (erosion + 0.70f) / 0.30f;
        return low_mtn + t * (mtn - low_mtn);
    }
    if (erosion <= -0.35f) {
        float t = (erosion + 0.40f) / 0.05f;
        return mtn + t * (wide_plateau - mtn);
    }
    if (erosion <= -0.10f) {
        float t = (erosion + 0.35f) / 0.25f;
        return wide_plateau + t * (narrow_plateau - wide_plateau);
    }
    if (erosion <= 0.20f) {
        float t = (erosion + 0.10f) / 0.30f;
        return narrow_plateau + t * (plains - narrow_plateau);
    }
    if (include_extreme_hills && erosion <= 0.58f) {
        float extreme_hills = low_valley + (ridges + 1.0f) * 0.5f * (tall_hill + 0.07f - low_valley);
        if (erosion <= 0.45f) {
            float t = (erosion - 0.20f) / 0.25f;
            return plains + t * (extreme_hills - plains);
        } else if (erosion <= 0.55f) {
            return extreme_hills;
        } else {
            float t = (erosion - 0.55f) / 0.03f;
            return extreme_hills + t * (plains - extreme_hills);
        }
    }
    if (erosion <= 0.70f) {
        float t = (erosion - 0.20f) / 0.50f;
        return plains + t * (swamps - plains);
    }
    return swamps;
}

/* 1:1 C implementation of Mojang 26.3 TerrainProvider.overworldOffset */
static inline float eval_overworld_offset(float continents, float erosion, float ridges) {
    float beach_spline = eval_erosion_offset(erosion, ridges, -0.15f, 0.0f, 0.0f, 0.1f, 0.0f, -0.03f, false, false);
    float low_spline   = eval_erosion_offset(erosion, ridges, -0.10f, 0.03f, 0.10f, 0.10f, 0.01f, -0.03f, false, false);
    float mid_spline   = eval_erosion_offset(erosion, ridges, -0.10f, 0.03f, 0.10f, 0.70f, 0.01f, -0.03f, true, true);
    float high_spline  = eval_erosion_offset(erosion, ridges, -0.05f, 0.03f, 0.10f, 1.00f, 0.01f, 0.01f, true, true);

    if (continents <= -1.10f) return 0.044f;
    if (continents <= -1.02f) {
        float t = (continents + 1.10f) / 0.08f;
        return 0.044f + t * (-0.2222f - 0.044f);
    }
    if (continents <= -0.51f) return -0.2222f;
    if (continents <= -0.44f) {
        float t = (continents + 0.51f) / 0.07f;
        return -0.2222f + t * (-0.12f - (-0.2222f));
    }
    if (continents <= -0.18f) return -0.12f;
    if (continents <= -0.15f) {
        float t = (continents + 0.18f) / 0.03f;
        return -0.12f + t * (beach_spline - (-0.12f));
    }
    if (continents <= -0.10f) {
        float t = (continents + 0.15f) / 0.05f;
        return beach_spline + t * (low_spline - beach_spline);
    }
    if (continents <= 0.25f) {
        float t = (continents + 0.10f) / 0.35f;
        return low_spline + t * (mid_spline - low_spline);
    }
    if (continents <= 1.00f) {
        float t = (continents - 0.25f) / 0.75f;
        return mid_spline + t * (high_spline - mid_spline);
    }
    return high_spline;
}

static int terrain_height(int x, int z, uint64_t seed) {
    float c = get_continentalness(x, z, seed);
    float e = get_erosion(x, z, seed);
    float w = get_weirdness(x, z, seed);
    float ridges = peaks_and_valleys(w);

    float offset = eval_overworld_offset(c, e, ridges);
    float h = (float)SEA_LEVEL + offset * 128.0f;
    h += fbm2(x * 0.040f, z * 0.040f, seed ^ 0x98765432ULL, 2) * 2.0f;

    if (h < 8.0f) h = 8.0f;
    if (h > (float)(WORLD_HEIGHT - 12)) h = (float)(WORLD_HEIGHT - 12);
    return (int)floorf(h);
}

/* Mojang 26.3 OverworldBiomeBuilder climate indices */
static inline int get_temperature_index(float t) {
    if (t < -0.45f) return 0;
    if (t < -0.15f) return 1;
    if (t <  0.20f) return 2;
    if (t <  0.55f) return 3;
    return 4;
}

static inline int get_humidity_index(float h) {
    if (h < -0.35f) return 0;
    if (h < -0.10f) return 1;
    if (h <  0.10f) return 2;
    if (h <  0.30f) return 3;
    return 4;
}

static inline int get_erosion_index(float e) {
    if (e < -0.78f)   return 0;
    if (e < -0.375f)  return 1;
    if (e < -0.2225f) return 2;
    if (e <  0.05f)   return 3;
    if (e <  0.45f)   return 4;
    if (e <  0.55f)   return 5;
    return 6;
}

/* 1:1 26.3 Overworld Biome matrices from OverworldBiomeBuilder.class */
static const Biome MIDDLE_BIOMES[5][5] = {
    { BIOME_SNOWY_PLAINS, BIOME_SNOWY_PLAINS, BIOME_SNOWY_PLAINS, BIOME_SNOWY_TAIGA, BIOME_TAIGA },
    { BIOME_PLAINS,       BIOME_PLAINS,       BIOME_FOREST,       BIOME_TAIGA,       BIOME_OLD_GROWTH_SPRUCE_TAIGA },
    { BIOME_FLOWER_FOREST,BIOME_PLAINS,       BIOME_FOREST,       BIOME_BIRCH_FOREST,BIOME_DARK_FOREST },
    { BIOME_SAVANNA,      BIOME_SAVANNA,      BIOME_FOREST,       BIOME_JUNGLE,      BIOME_JUNGLE },
    { BIOME_DESERT,       BIOME_DESERT,       BIOME_DESERT,       BIOME_DESERT,      BIOME_DESERT }
};

static const Biome MIDDLE_BIOMES_VARIANT[5][5] = {
    { BIOME_ICE_SPIKES,     BIOME_SNOWY_PLAINS, BIOME_SNOWY_TAIGA,  BIOME_SNOWY_TAIGA,             BIOME_TAIGA },
    { BIOME_DAPPLED_FOREST, BIOME_PLAINS,       BIOME_FOREST,       BIOME_TAIGA,                   BIOME_OLD_GROWTH_PINE_TAIGA },
    { BIOME_SUNFLOWER_PLAINS,BIOME_PLAINS,      BIOME_FOREST,       BIOME_OLD_GROWTH_BIRCH_FOREST, BIOME_DARK_FOREST },
    { BIOME_SAVANNA,        BIOME_SAVANNA,      BIOME_PLAINS,       BIOME_SPARSE_JUNGLE,           BIOME_BAMBOO_JUNGLE },
    { BIOME_DESERT,         BIOME_DESERT,       BIOME_DESERT,       BIOME_DESERT,                  BIOME_DESERT }
};

static const Biome PLATEAU_BIOMES[5][5] = {
    { BIOME_SNOWY_PLAINS,   BIOME_SNOWY_PLAINS,   BIOME_SNOWY_PLAINS, BIOME_SNOWY_TAIGA, BIOME_SNOWY_TAIGA },
    { BIOME_MEADOW,         BIOME_MEADOW,         BIOME_FOREST,       BIOME_TAIGA,       BIOME_OLD_GROWTH_SPRUCE_TAIGA },
    { BIOME_MEADOW,         BIOME_MEADOW,         BIOME_MEADOW,       BIOME_MEADOW,      BIOME_PALE_GARDEN },
    { BIOME_SAVANNA_PLATEAU,BIOME_SAVANNA_PLATEAU,BIOME_FOREST,       BIOME_FOREST,      BIOME_JUNGLE },
    { BIOME_BADLANDS,       BIOME_BADLANDS,       BIOME_BADLANDS,     BIOME_WOODED_BADLANDS, BIOME_WOODED_BADLANDS }
};

static const Biome PLATEAU_BIOMES_VARIANT[5][5] = {
    { BIOME_ICE_SPIKES,     BIOME_SNOWY_PLAINS,   BIOME_SNOWY_PLAINS, BIOME_SNOWY_TAIGA, BIOME_SNOWY_TAIGA },
    { BIOME_CHERRY_GROVE,   BIOME_MEADOW,         BIOME_MEADOW,       BIOME_MEADOW,      BIOME_OLD_GROWTH_PINE_TAIGA },
    { BIOME_CHERRY_GROVE,   BIOME_CHERRY_GROVE,   BIOME_FOREST,       BIOME_BIRCH_FOREST,BIOME_PALE_GARDEN },
    { BIOME_SAVANNA_PLATEAU,BIOME_SAVANNA_PLATEAU,BIOME_FOREST,       BIOME_FOREST,      BIOME_JUNGLE },
    { BIOME_ERODED_BADLANDS,BIOME_ERODED_BADLANDS,BIOME_BADLANDS,     BIOME_WOODED_BADLANDS, BIOME_WOODED_BADLANDS }
};

static const Biome SHATTERED_BIOMES[5][5] = {
    { BIOME_WINDSWEPT_GRAVELLY_HILLS, BIOME_WINDSWEPT_GRAVELLY_HILLS, BIOME_WINDSWEPT_HILLS, BIOME_WINDSWEPT_FOREST, BIOME_WINDSWEPT_FOREST },
    { BIOME_WINDSWEPT_GRAVELLY_HILLS, BIOME_WINDSWEPT_GRAVELLY_HILLS, BIOME_WINDSWEPT_HILLS, BIOME_WINDSWEPT_FOREST, BIOME_WINDSWEPT_FOREST },
    { BIOME_WINDSWEPT_HILLS,          BIOME_WINDSWEPT_HILLS,          BIOME_WINDSWEPT_HILLS, BIOME_WINDSWEPT_FOREST, BIOME_WINDSWEPT_FOREST },
    { BIOME_WINDSWEPT_SAVANNA,        BIOME_WINDSWEPT_SAVANNA,        BIOME_WINDSWEPT_SAVANNA,BIOME_WINDSWEPT_SAVANNA,BIOME_WINDSWEPT_SAVANNA },
    { BIOME_DESERT,                   BIOME_DESERT,                   BIOME_DESERT,          BIOME_DESERT,           BIOME_DESERT }
};

static inline Biome pick_badlands_biome(int hi, float w) {
    if (hi < 2) return (w < 0.0f) ? BIOME_BADLANDS : BIOME_ERODED_BADLANDS;
    if (hi < 3) return BIOME_BADLANDS;
    return BIOME_WOODED_BADLANDS;
}

static inline Biome pick_middle_biome(int ti, int hi, float w) {
    return (w < 0.0f) ? MIDDLE_BIOMES[ti][hi] : MIDDLE_BIOMES_VARIANT[ti][hi];
}

static inline Biome pick_plateau_biome(int ti, int hi, float w) {
    return (w >= 0.0f) ? PLATEAU_BIOMES_VARIANT[ti][hi] : PLATEAU_BIOMES[ti][hi];
}

static inline Biome pick_peak_biome(int ti, int hi, float w) {
    if (ti <= 2) return (w < 0.0f) ? BIOME_JAGGED_PEAKS : BIOME_FROZEN_PEAKS;
    if (ti == 3) return BIOME_STONY_PEAKS;
    return pick_badlands_biome(hi, w);
}

static inline Biome pick_slope_biome(int ti, int hi, float w) {
    if (ti >= 3) return pick_plateau_biome(ti, hi, w);
    if (hi <= 1) return BIOME_SNOWY_SLOPES;
    return BIOME_GROVE;
}

static inline Biome pick_beach_biome(int ti) {
    if (ti == 0) return BIOME_SNOWY_BEACH;
    if (ti == 4) return BIOME_DESERT;
    return BIOME_BEACH;
}

/* 1:1 OverworldBiomeBuilder dispatch table from Minecraft 26.3 */
static Biome biome_at(int x, int z, int h, uint64_t seed) {
    /* Coordinate jitter / domain warping: removes harsh knife-edge borders */
    float jx = noise2((float)x * 0.035f, (float)z * 0.035f, seed ^ 0x71717171ULL) * 12.0f;
    float jz = noise2((float)x * 0.035f + 128.0f, (float)z * 0.035f + 128.0f, seed ^ 0x35353535ULL) * 12.0f;
    int qx = x + (int)jx;
    int qz = z + (int)jz;

    float c = get_continentalness(qx, qz, seed);
    float e = get_erosion(qx, qz, seed);
    float w = get_weirdness(qx, qz, seed);
    float temp = get_temperature(qx, qz, seed);
    float hum = get_humidity(qx, qz, seed);
    float pv = peaks_and_valleys(w);

    int ti = get_temperature_index(temp);
    int hi = get_humidity_index(hum);
    int ei = get_erosion_index(e);

    /* Mushroom fields */
    if (c < -1.05f) return BIOME_MUSHROOM_FIELDS;
    /* Deep ocean */
    if (c < -0.455f) return BIOME_DEEP_OCEAN;
    /* Ocean */
    if (c < -0.19f) return BIOME_OCEAN;
    /* Coast / Beach / Stony Shore */
    if (c < -0.11f) {
        if (ei <= 2) return BIOME_STONY_SHORE;
        return pick_beach_biome(ti);
    }

    /* High Mountain Peaks (Erosion 0 or peaks & valleys or high elevation) */
    if (ei == 0 || h >= 140 || (ei <= 1 && pv > 0.40f)) {
        return pick_peak_biome(ti, hi, w);
    }

    /* Mountain Slopes & Plateaus (Erosion 1-2 or high elevation) */
    if (ei <= 2 || h >= 92) {
        if (ti == 0) return pick_slope_biome(ti, hi, w);
        return pick_plateau_biome(ti, hi, w);
    }

    /* Shattered & Windswept Terrain (Erosion 5) */
    if (ei == 5 && pv > 0.15f) {
        return SHATTERED_BIOMES[ti][hi];
    }

    /* Lowlands & Swamps (Erosion 6) */
    if (ei == 6) {
        if (ti >= 3 && hi >= 2) return BIOME_MANGROVE_SWAMP;
        if (ti >= 1 && hi >= 2) return BIOME_SWAMP;
        return pick_middle_biome(ti, hi, w);
    }

    /* Middle inland terrain */
    if (ti == 4) return pick_badlands_biome(hi, w);
    return pick_middle_biome(ti, hi, w);
}

/* 26.3 Underground Biome selector for 3D caverns */
static inline Biome underground_biome_at(int x, int y, int z, uint64_t seed) {
    float jx = noise2((float)x * 0.035f, (float)z * 0.035f, seed ^ 0x71717171ULL) * 12.0f;
    float jz = noise2((float)x * 0.035f + 128.0f, (float)z * 0.035f + 128.0f, seed ^ 0x35353535ULL) * 12.0f;
    int qx = x + (int)jx;
    int qz = z + (int)jz;

    float c = get_continentalness(qx, qz, seed);
    float e = get_erosion(qx, qz, seed);
    float w = get_weirdness(qx, qz, seed);
    float hum = get_humidity(qx, qz, seed);

    /* 26.3 Sulfur Caves: c in [-0.19, 0.55], e in [0.45, 1.0], w in [-1.1, -0.65] */
    if (c >= -0.19f && c <= 0.55f && e >= 0.45f && w <= -0.65f)
        return BIOME_SULFUR_CAVES;

    /* Lush Caves: humidity >= 0.40 */
    if (hum >= 0.40f)
        return BIOME_LUSH_CAVES;

    /* Dripstone Caves: continentalness >= 0.15 & erosion < 0.15 & hum < 0.35 */
    if (c >= 0.15f && e < 0.15f && hum < 0.35f)
        return BIOME_DRIPSTONE_CAVES;

    /* Deep Dark: bottom bedrock layer (Y <= 28) and mountain roots (erosion < -0.30) */
    if (y <= 28 && e < -0.30f)
        return BIOME_DEEP_DARK;

    return BIOME_PLAINS;
}

static inline uint8_t get_terracotta_strata(int y) {
    int band = (y + 12) % 16;
    switch (band) {
    case 0: case 1: return BLOCK_RED_TERRACOTTA;
    case 2: case 3: return BLOCK_ORANGE_TERRACOTTA;
    case 4: case 5: return BLOCK_YELLOW_TERRACOTTA;
    case 6: return BLOCK_WHITE_TERRACOTTA;
    case 7: case 8: return BLOCK_TERRACOTTA;
    case 9: case 10: return BLOCK_LIGHT_GRAY_TERRACOTTA;
    case 11: case 12: return BLOCK_BROWN_TERRACOTTA;
    case 13: return BLOCK_ORANGE_TERRACOTTA;
    default: return BLOCK_TERRACOTTA;
    }
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

static void place_cherry_tree(World *w, Chunk *c, int lx, int lz, int h, uint64_t seed) {
    (void)w;
    uint64_t rnd = (uint64_t)mced_hash_u32((uint32_t)(c->cx * CHUNK_SIZE + lx) * 374761393U ^
                                          (uint32_t)(c->cz * CHUNK_SIZE + lz) * 668265263U ^
                                          (uint32_t)seed);
    int height = 5 + (int)(rnd % 3);
    int dx_branch = (rnd & 1) ? 1 : -1;
    int dz_branch = (rnd & 2) ? 1 : -1;

    /* S-curved trunk of Cherry Log */
    for (int dy = 1; dy <= height; dy++) {
        int ox = (dy >= height - 2) ? dx_branch : 0;
        int oz = (dy >= height - 2) ? dz_branch : 0;
        set_local(c, lx + ox, h + dy, lz + oz, BLOCK_CHERRY_LOG);
    }

    int top = h + height;
    int cx = lx + dx_branch;
    int cz = lz + dz_branch;

    /* Wide blossoming cherry leaves canopy */
    for (int dy = -1; dy <= 2; dy++) {
        int rad = (dy <= 0) ? 3 : (dy == 1 ? 2 : 1);
        for (int ox = -rad; ox <= rad; ox++) {
            for (int oz = -rad; oz <= rad; oz++) {
                if (abs(ox) == rad && abs(oz) == rad && dy >= 1) continue;
                if (ox == 0 && oz == 0 && dy <= 0) continue;
                if (get_local(c, cx + ox, top + dy, cz + oz) == BLOCK_AIR)
                    set_local(c, cx + ox, top + dy, cz + oz, BLOCK_CHERRY_LEAVES);
            }
        }
    }
}

static void place_pale_tree(World *w, Chunk *c, int lx, int lz, int h, uint64_t seed) {
    (void)w;
    uint64_t rnd = (uint64_t)mced_hash_u32((uint32_t)(c->cx * CHUNK_SIZE + lx) * 2147483647U ^
                                          (uint32_t)(c->cz * CHUNK_SIZE + lz) * 16807U ^
                                          (uint32_t)seed);
    int height = 7 + (int)(rnd % 4);
    int heart_y = h + 2 + (int)(rnd % 3);
    bool has_heart = ((rnd % 4U) == 0);

    for (int dy = 1; dy <= height; dy++) {
        uint8_t blk = (has_heart && (h + dy == heart_y)) ? BLOCK_CREAKING_HEART : BLOCK_PALE_OAK_LOG;
        set_local(c, lx, h + dy, lz, blk);
    }

    int top = h + height;
    for (int dy = -3; dy <= 1; dy++) {
        int rad = (dy <= -1) ? 2 : 1;
        for (int ox = -rad; ox <= rad; ox++) {
            for (int oz = -rad; oz <= rad; oz++) {
                if (abs(ox) == rad && abs(oz) == rad && (dy == 1 || (dy == 0 && (rnd & 1))))
                    continue;
                if (ox == 0 && oz == 0 && dy <= 0) continue;
                if (get_local(c, lx + ox, top + dy, lz + oz) == BLOCK_AIR) {
                    set_local(c, lx + ox, top + dy, lz + oz, BLOCK_PALE_OAK_LEAVES);
                    if (dy == -3 && ((rnd + (uint64_t)(ox * 7 + oz * 13)) % 3U == 0)) {
                        int moss_len = 1 + (int)((rnd >> 4) % 3U);
                        for (int m = 1; m <= moss_len; m++) {
                            if (get_local(c, lx + ox, top + dy - m, lz + oz) == BLOCK_AIR)
                                set_local(c, lx + ox, top + dy - m, lz + oz, BLOCK_PALE_HANGING_MOSS);
                        }
                    }
                }
            }
        }
    }
}

static void place_amethyst_geode(Chunk *c, int gx, int gy, int gz, uint64_t seed) {
    (void)seed;
    int rad = 4;
    for (int dx = -rad; dx <= rad; dx++) {
        for (int dy = -rad; dy <= rad; dy++) {
            for (int dz = -rad; dz <= rad; dz++) {
                int r2 = dx * dx + dy * dy + dz * dz;
                int bx = gx + dx;
                int by = gy + dy;
                int bz = gz + dz;
                if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE || by < 3 || by >= WORLD_HEIGHT - 4)
                    continue;
                if (r2 <= 2) {
                    set_local(c, bx, by, bz, (by <= 14) ? BLOCK_WATER : BLOCK_AIR);
                } else if (r2 <= 5) {
                    set_local(c, bx, by, bz, BLOCK_AMETHYST_BLOCK);
                } else if (r2 <= 9) {
                    set_local(c, bx, by, bz, BLOCK_CALCITE);
                } else if (r2 <= 14) {
                    set_local(c, bx, by, bz, BLOCK_TUFF);
                }
            }
        }
    }
}

static void place_tree(World *w, Chunk *c, int lx, int lz, int h, uint64_t seed) {
    (void)w;
    uint64_t rnd = (uint64_t)mced_hash_u32((uint32_t)(c->cx * CHUNK_SIZE + lx) * 374761393U ^
                                          (uint32_t)(c->cz * CHUNK_SIZE + lz) * 668265263U ^
                                          (uint32_t)seed);
    int height = 4 + (int)(rnd % 3);
    BlockId log = BLOCK_OAK_LOG;
    BlockId leaves = BLOCK_OAK_LEAVES;

    float temp = get_temperature(c->cx * CHUNK_SIZE + lx, c->cz * CHUNK_SIZE + lz, seed);
    float hum = get_humidity(c->cx * CHUNK_SIZE + lx, c->cz * CHUNK_SIZE + lz, seed);
    if (temp < -0.15f) {
        log = BLOCK_SPRUCE_LOG;
        leaves = BLOCK_SPRUCE_LEAVES;
        height = 6 + (int)(rnd % 4);
    } else if (hum > 0.35f) {
        log = BLOCK_DARK_OAK_LOG;
        leaves = BLOCK_DARK_OAK_LEAVES;
        height = 5 + (int)(rnd % 2);
    } else if (hum > 0.22f) {
        log = BLOCK_BIRCH_LOG;
        leaves = BLOCK_BIRCH_LEAVES;
    } else if (hum > 0.15f && temp > 0.2f) {
        log = BLOCK_JUNGLE_LOG;
        leaves = BLOCK_JUNGLE_LEAVES;
        height = 6 + (int)(rnd % 5);
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

static void place_tall_birch(Chunk *c, int lx, int lz, int h, uint64_t seed) {
    uint64_t rnd = (uint64_t)mced_hash_u32((uint32_t)(c->cx * CHUNK_SIZE + lx) * 374761393U ^
                                          (uint32_t)(c->cz * CHUNK_SIZE + lz) * 668265263U ^
                                          (uint32_t)seed);
    int height = 7 + (int)(rnd % 4);
    for (int dy = 1; dy <= height; dy++)
        set_local(c, lx, h + dy, lz, BLOCK_BIRCH_LOG);

    int top = h + height;
    for (int dy = -2; dy <= 1; dy++) {
        int radius = (dy <= -1) ? 2 : 1;
        for (int dx = -radius; dx <= radius; dx++) {
            for (int dz = -radius; dz <= radius; dz++) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (abs(dx) == radius && abs(dz) == radius && (dy == 1 || (dy == 0 && (rnd & 1)))) continue;
                if (get_local(c, lx + dx, top + dy, lz + dz) == BLOCK_AIR)
                    set_local(c, lx + dx, top + dy, lz + dz, BLOCK_BIRCH_LEAVES);
            }
        }
    }
}

static void place_mega_spruce(Chunk *c, int lx, int lz, int h, uint64_t seed) {
    uint64_t rnd = (uint64_t)mced_hash_u32((uint32_t)(c->cx * CHUNK_SIZE + lx) * 374761393U ^
                                          (uint32_t)(c->cz * CHUNK_SIZE + lz) * 668265263U ^
                                          (uint32_t)seed);
    int height = 9 + (int)(rnd % 5);
    for (int dy = 1; dy <= height; dy++)
        set_local(c, lx, h + dy, lz, BLOCK_SPRUCE_LOG);

    int top = h + height;
    for (int dy = -5; dy <= 1; dy++) {
        int radius = (dy <= -3) ? 2 : ((dy <= -1) ? 2 : (dy == 0 ? 1 : 0));
        for (int dx = -radius; dx <= radius; dx++) {
            for (int dz = -radius; dz <= radius; dz++) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (abs(dx) == radius && abs(dz) == radius && (dy % 2 == 0)) continue;
                if (get_local(c, lx + dx, top + dy, lz + dz) == BLOCK_AIR)
                    set_local(c, lx + dx, top + dy, lz + dz, BLOCK_SPRUCE_LEAVES);
            }
        }
    }
}

static void place_ice_spike(Chunk *c, int lx, int lz, int h, uint64_t seed) {
    uint64_t rnd = (uint64_t)mced_hash_u32((uint32_t)(c->cx * CHUNK_SIZE + lx) * 1234567U ^
                                          (uint32_t)(c->cz * CHUNK_SIZE + lz) * 7654321U ^
                                          (uint32_t)seed);
    int height = 10 + (int)(rnd % 10);
    for (int dy = 1; dy <= height; dy++) {
        int rad = (dy <= height / 3) ? 2 : ((dy <= height * 2 / 3) ? 1 : 0);
        for (int dx = -rad; dx <= rad; dx++) {
            for (int dz = -rad; dz <= rad; dz++) {
                if (abs(dx) == rad && abs(dz) == rad && rad > 0 && (rnd & 1)) continue;
                uint8_t blk = ((rnd + (uint64_t)dy) % 3 == 0) ? BLOCK_BLUE_ICE : BLOCK_PACKED_ICE;
                set_local(c, lx + dx, h + dy, lz + dz, blk);
            }
        }
    }
}

static inline uint8_t pick_meadow_flower(uint32_t flor) {
    switch (flor % 9U) {
    case 0: return BLOCK_ALLIUM;
    case 1: return BLOCK_CORNFLOWER;
    case 2: return BLOCK_OXEYE_DAISY;
    case 3: return BLOCK_RED_TULIP;
    case 4: return BLOCK_ORANGE_TULIP;
    case 5: return BLOCK_WHITE_TULIP;
    case 6: return BLOCK_PINK_TULIP;
    case 7: return BLOCK_DANDELION;
    default: return BLOCK_POPPY;
    }
}

static inline uint8_t pick_plains_flower(uint32_t flor) {
    switch (flor % 7U) {
    case 0: return BLOCK_DANDELION;
    case 1: return BLOCK_POPPY;
    case 2: return BLOCK_AZURE_BLUET;
    case 3: return BLOCK_CORNFLOWER;
    case 4: return BLOCK_OXEYE_DAISY;
    case 5: return BLOCK_RED_TULIP;
    default: return BLOCK_WHITE_TULIP;
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
            bool is_badlands = (biome == BIOME_BADLANDS || biome == BIOME_ERODED_BADLANDS || biome == BIOME_WOODED_BADLANDS);

            if (h <= SEA_LEVEL) {
                /* Underwater bed: NEVER grass block! Authentic sand, gravel, clay, and mud */
                uint32_t wr = mced_hash_u32((uint32_t)wx * 0x85ebca6bU ^ (uint32_t)wz * 0xc2b2ae35U ^ (uint32_t)w->seed);
                if (biome == BIOME_OCEAN || biome == BIOME_DEEP_OCEAN) {
                    float temp = get_temperature(wx, wz, w->seed);
                    if (temp > 0.15f) {
                        top = BLOCK_SAND;
                        filler = BLOCK_SAND;
                    } else if (temp < -0.15f) {
                        top = ((wr % 3U) == 0) ? BLOCK_GRAVEL : (((wr % 7U) == 0) ? BLOCK_CLAY : BLOCK_SAND);
                        filler = (top == BLOCK_GRAVEL) ? BLOCK_GRAVEL : BLOCK_SAND;
                    } else {
                        top = ((wr % 7U) == 0) ? BLOCK_GRAVEL : (((wr % 15U) == 0) ? BLOCK_CLAY : BLOCK_SAND);
                        filler = (top == BLOCK_GRAVEL) ? BLOCK_GRAVEL : BLOCK_SAND;
                    }
                } else if (biome == BIOME_SWAMP || biome == BIOME_MANGROVE_SWAMP) {
                    top = ((wr % 3U) == 0) ? BLOCK_CLAY : BLOCK_MUD;
                    filler = BLOCK_PACKED_MUD;
                } else {
                    /* Rivers, lakes, flooded shores in inland biomes */
                    uint32_t rbed = wr % 10U;
                    if (rbed < 6) {
                        top = BLOCK_SAND;
                        filler = BLOCK_SAND;
                    } else if (rbed < 8) {
                        top = BLOCK_GRAVEL;
                        filler = BLOCK_GRAVEL;
                    } else if (rbed < 9) {
                        top = BLOCK_CLAY;
                        filler = BLOCK_DIRT;
                    } else {
                        top = BLOCK_DIRT;
                        filler = BLOCK_DIRT;
                    }
                }
            } else if (biome == BIOME_OCEAN || biome == BIOME_DEEP_OCEAN || biome == BIOME_BEACH || biome == BIOME_DESERT) {
                top = BLOCK_SAND;
                filler = (biome == BIOME_DESERT) ? BLOCK_SANDSTONE : BLOCK_SAND;
            } else if (biome == BIOME_SNOWY_PLAINS || biome == BIOME_SNOWY_TAIGA || biome == BIOME_SNOWY_SLOPES || biome == BIOME_SNOWY_BEACH) {
                top = BLOCK_SNOW_BLOCK;
                filler = BLOCK_DIRT;
            } else if (biome == BIOME_JAGGED_PEAKS || biome == BIOME_FROZEN_PEAKS) {
                uint32_t pr = mced_hash_u32((uint32_t)wx * 0x31415926U ^ (uint32_t)wz * 0x27182818U ^ (uint32_t)w->seed);
                if ((pr & 7) == 0) top = BLOCK_BLUE_ICE;
                else if ((pr & 3) == 0) top = BLOCK_PACKED_ICE;
                else if ((pr & 15) == 1) top = BLOCK_CALCITE;
                else top = BLOCK_SNOW_BLOCK;
                filler = BLOCK_STONE;
            } else if (biome == BIOME_STONY_PEAKS || biome == BIOME_STONY_SHORE) {
                uint32_t pr = mced_hash_u32((uint32_t)wx * 0x31415926U ^ (uint32_t)wz * 0x27182818U ^ (uint32_t)w->seed);
                top = ((pr & 7) == 0) ? BLOCK_CALCITE : BLOCK_STONE;
                filler = BLOCK_STONE;
            } else if (biome == BIOME_SWAMP || biome == BIOME_MANGROVE_SWAMP) {
                top = (h <= SEA_LEVEL + 1) ? BLOCK_MUD : BLOCK_GRASS_BLOCK;
                filler = BLOCK_PACKED_MUD;
            } else if (biome == BIOME_PALE_GARDEN) {
                top = BLOCK_PALE_MOSS_BLOCK;
                filler = BLOCK_DIRT;
            } else if (biome == BIOME_OLD_GROWTH_PINE_TAIGA || biome == BIOME_OLD_GROWTH_SPRUCE_TAIGA) {
                uint32_t pod = mced_hash_u32((uint32_t)wx * 37U ^ (uint32_t)wz * 101U);
                top = (pod & 3) ? BLOCK_PODZOL : BLOCK_COARSE_DIRT;
                filler = BLOCK_DIRT;
            }

            for (int y = 0; y <= h; y++) {
                uint8_t b;
                if (y == 0) {
                    b = BLOCK_BEDROCK;
                } else if (y <= 3 && (mced_hash_u32((uint32_t)(wx * 31 + wz * 17 + y) ^ (uint32_t)w->seed) & 3) == 0) {
                    b = BLOCK_BEDROCK;
                } else if (is_badlands) {
                    if (y == h && biome == BIOME_WOODED_BADLANDS && h > 90) {
                        b = BLOCK_COARSE_DIRT;
                    } else if (y == h && h <= 74) {
                        b = BLOCK_RED_SAND;
                    } else if (y >= h - 2 && h <= 74) {
                        b = BLOCK_RED_SANDSTONE;
                    } else if (y >= h - 2) {
                        b = BLOCK_RED_TERRACOTTA;
                    } else if (y >= 32) {
                        b = get_terracotta_strata(y);
                    } else if (y >= 16) {
                        b = ((y + (wx ^ wz)) & 3) ? BLOCK_DEEPSLATE : BLOCK_TERRACOTTA;
                    } else {
                        b = BLOCK_DEEPSLATE;
                    }
                } else if (y == h) {
                    b = top;
                } else if (y >= h - 3) {
                    b = filler;
                } else {
                    /* Subsurface strata: Deepslate below Y=32 with dithered transition */
                    if (y < 16) {
                        b = BLOCK_DEEPSLATE;
                    } else if (y < 32) {
                        uint32_t dither = mced_hash_u32((uint32_t)wx * 101U ^ (uint32_t)wz * 37U ^ (uint32_t)y * 997U);
                        b = ((dither % 16U) > (uint32_t)(y - 16)) ? BLOCK_DEEPSLATE : BLOCK_STONE;
                    } else {
                        b = BLOCK_STONE;
                    }
                }
                c->blocks[chunk_index_local(lx, y, lz)] = b;
            }

            for (int y = h + 1; y <= SEA_LEVEL; y++)
                c->blocks[chunk_index_local(lx, y, lz)] = BLOCK_WATER;

            c->heightmap[lx + lz * CHUNK_SIZE] = (uint16_t)h;
        }
    }

    /* 3D Cave systems: Minecraft 26.3 Cheese caverns, Spaghetti tunnels, noodle branches & ravines */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = c->heightmap[lx + lz * CHUNK_SIZE];

            /* Ocean and surface protection: never puncture ocean floor or river beds */
            int max_carve_y;
            if (h <= SEA_LEVEL + 2) {
                max_carve_y = h - 6; /* Protect ocean and river beds */
            } else {
                max_carve_y = h;     /* Allow natural cave entrances and canyon mouths on land */
            }
            if (max_carve_y <= 4)
                continue;

            /* Vary tunnel thickness along the winding path (2.5 to 5.5 blocks diameter) */
            float t_thick = 0.0072f + 0.0036f * noise3(wx * 0.016f, 32.0f * 0.022f, wz * 0.016f, w->seed ^ 0xABCDEF01ULL);

            for (int y = 4; y <= max_carve_y; y++) {
                uint8_t b = c->blocks[chunk_index_local(lx, y, lz)];
                if (b == BLOCK_AIR || b == BLOCK_BEDROCK || b == BLOCK_WATER || b == BLOCK_LAVA)
                    continue;

                /* 1. Primary Spaghetti Tunnel (smooth winding 3D cylinder) */
                float a1 = noise3(wx * 0.026f, y * 0.034f, wz * 0.026f, w->seed ^ 0x10203040ULL);
                float a2 = noise3(wx * 0.026f, y * 0.034f, wz * 0.026f, w->seed ^ 0x50607080ULL);
                float d_tunnel = a1 * a1 + a2 * a2;

                /* 2. Secondary Cross-Connecting Branch Network */
                float b1 = noise3(wx * 0.022f, y * 0.028f, wz * 0.022f, w->seed ^ 0x90A0B0C0ULL);
                float b2 = noise3(wx * 0.022f, y * 0.028f, wz * 0.022f, w->seed ^ 0xD0E0F001ULL);
                float d_branch = b1 * b1 + b2 * b2;

                bool carve = (d_tunnel < t_thick) || (d_branch < t_thick * 0.85f);

                /* 3. Cheese Caverns: Vast vaulted halls, domed chambers, and natural stone pillars */
                if (!carve && y >= 10 && y <= 56) {
                    float env = sinf((float)(y - 10) / (56.0f - 10.0f) * 3.14159f);
                    if (env > 0.22f) {
                        float cheese = fbm3(wx * 0.014f, y * 0.018f, wz * 0.014f, w->seed ^ 0xCAFE7777ULL, 2);
                        float pillar = noise2(wx * 0.032f, wz * 0.032f, w->seed ^ 0x88884444ULL);
                        if (cheese * env > 0.36f && fabsf(pillar) > 0.12f) {
                            carve = true;
                        }
                    }
                }

                /* 4. Sinuous Canyons & Deep Ravines */
                if (!carve && y >= 12 && y <= h - 1 && h > SEA_LEVEL + 2) {
                    float rav = noise2(wx * 0.007f, wz * 0.007f, w->seed ^ 0xAA55AA55ULL);
                    float twist = noise3(wx * 0.020f, y * 0.035f, wz * 0.020f, w->seed ^ 0x55AA55AAULL) * 0.030f;
                    float dist = fabsf(rav + twist);
                    float span = 1.0f - fabsf((float)y - 34.0f) / 38.0f;
                    if (span > 0.0f) {
                        float rav_w = 0.016f * span;
                        if (dist < rav_w) {
                            carve = true;
                        }
                    }
                }

                if (carve) {
                    /* Authentic Minecraft Y <= 11 lava lake fill */
                    c->blocks[chunk_index_local(lx, y, lz)] = (y <= 11) ? BLOCK_LAVA : BLOCK_AIR;
                }
            }

            /* Natural cave mouth surface cleanup: adjust heightmap and ground flora */
            if (c->blocks[chunk_index_local(lx, h, lz)] == BLOCK_AIR) {
                int new_h = h - 1;
                while (new_h > 0 && c->blocks[chunk_index_local(lx, new_h, lz)] == BLOCK_AIR) {
                    new_h--;
                }
                c->heightmap[lx + lz * CHUNK_SIZE] = (uint16_t)new_h;
                if (new_h > SEA_LEVEL) {
                    uint8_t floor_b = c->blocks[chunk_index_local(lx, new_h, lz)];
                    if (floor_b == BLOCK_DIRT || floor_b == BLOCK_STONE) {
                        c->blocks[chunk_index_local(lx, new_h, lz)] = BLOCK_GRASS_BLOCK;
                    }
                }
            }
        }
    }

    /* 26.3 Underground Biome Decoration & Strata */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = c->heightmap[lx + lz * CHUNK_SIZE];
            for (int y = 5; y < h - 4; y++) {
                int idx = chunk_index_local(lx, y, lz);
                uint8_t cur = c->blocks[idx];
                Biome ubiome = underground_biome_at(wx, y, wz, w->seed);

                if (ubiome == BIOME_SULFUR_CAVES) {
                    /* Banded sulfur & cinnabar deposits on walls bordering caves */
                    if (cur == BLOCK_STONE || cur == BLOCK_DEEPSLATE) {
                        bool near_air = (c->blocks[chunk_index_local(lx, y + 1, lz)] == BLOCK_AIR ||
                                         c->blocks[chunk_index_local(lx, y - 1, lz)] == BLOCK_AIR);
                        if (near_air) {
                            float sband = sinf((float)y * 0.45f + (float)wx * 0.15f + (float)wz * 0.15f);
                            if (sband >= -0.40f && sband <= -0.10f) {
                                c->blocks[idx] = BLOCK_CINNABAR;
                            } else if (sband >= 0.00f && sband <= 0.40f) {
                                c->blocks[idx] = BLOCK_SULFUR;
                            } else if (sband > 0.40f) {
                                c->blocks[idx] = BLOCK_POTENT_SULFUR;
                            }
                        }
                    } else if (cur == BLOCK_AIR && y > 10) {
                        uint8_t below = c->blocks[chunk_index_local(lx, y - 1, lz)];
                        uint8_t above = c->blocks[chunk_index_local(lx, y + 1, lz)];
                        if (below == BLOCK_SULFUR || below == BLOCK_POTENT_SULFUR || below == BLOCK_CINNABAR) {
                            uint32_t sr = mced_hash_u32((uint32_t)wx * 11U ^ (uint32_t)wz * 23U ^ (uint32_t)y);
                            if ((sr % 5U) == 0)
                                c->blocks[idx] = BLOCK_SULFUR_SPIKE;
                        }
                        if (above == BLOCK_SULFUR || above == BLOCK_POTENT_SULFUR || above == BLOCK_CINNABAR) {
                            uint32_t sr = mced_hash_u32((uint32_t)wx * 17U ^ (uint32_t)wz * 29U ^ (uint32_t)y);
                            if ((sr % 5U) == 0)
                                c->blocks[idx] = BLOCK_SULFUR_SPIKE;
                        }
                    }
                } else if (ubiome == BIOME_LUSH_CAVES) {
                    /* Lush caves: moss floors, azalea bushes */
                    if (cur == BLOCK_AIR && y > 12) {
                        uint8_t below = c->blocks[chunk_index_local(lx, y - 1, lz)];
                        if (below == BLOCK_STONE || below == BLOCK_DEEPSLATE) {
                            c->blocks[chunk_index_local(lx, y - 1, lz)] = BLOCK_MOSS_BLOCK;
                            uint32_t lr = mced_hash_u32((uint32_t)wx * 19U ^ (uint32_t)wz * 43U ^ (uint32_t)y);
                            if ((lr % 5U) == 0) {
                                c->blocks[idx] = ((lr % 2U) == 0) ? BLOCK_AZALEA_LEAVES : BLOCK_FLOWERING_AZALEA_LEAVES;
                            }
                        }
                    }
                } else if (ubiome == BIOME_DRIPSTONE_CAVES) {
                    /* Authentic Minecraft Dripstone Caves: stone/deepslate walls with ceiling/floor dripstone patches & stalactites/stalagmites */
                    if (cur == BLOCK_AIR && y > 10 && y < h - 4) {
                        uint8_t below = c->blocks[chunk_index_local(lx, y - 1, lz)];
                        uint8_t above = c->blocks[chunk_index_local(lx, y + 1, lz)];

                        /* 1. Ceiling dripstone patch & hanging stalactite */
                        if (above == BLOCK_STONE || above == BLOCK_DEEPSLATE || above == BLOCK_DRIPSTONE_BLOCK) {
                            uint32_t cr = mced_hash_u32((uint32_t)wx * 31U ^ (uint32_t)wz * 37U ^ (uint32_t)(y / 3));
                            if ((cr % 100U) < 55) {
                                c->blocks[chunk_index_local(lx, y + 1, lz)] = BLOCK_DRIPSTONE_BLOCK;
                                if (y + 2 < h && (c->blocks[chunk_index_local(lx, y + 2, lz)] == BLOCK_STONE ||
                                                  c->blocks[chunk_index_local(lx, y + 2, lz)] == BLOCK_DEEPSLATE)) {
                                    c->blocks[chunk_index_local(lx, y + 2, lz)] = BLOCK_DRIPSTONE_BLOCK;
                                }
                                int st_len = 1 + (int)(cr % 4U);
                                for (int dy = 0; dy < st_len; dy++) {
                                    int sy = y - dy;
                                    if (sy > 10 && c->blocks[chunk_index_local(lx, sy, lz)] == BLOCK_AIR) {
                                        c->blocks[chunk_index_local(lx, sy, lz)] = BLOCK_POINTED_DRIPSTONE;
                                    } else {
                                        break;
                                    }
                                }
                            }
                        }

                        /* 2. Floor dripstone mound & rising stalagmite */
                        if (below == BLOCK_STONE || below == BLOCK_DEEPSLATE || below == BLOCK_DRIPSTONE_BLOCK) {
                            uint32_t fr = mced_hash_u32((uint32_t)wx * 19U ^ (uint32_t)wz * 43U ^ (uint32_t)(y / 3));
                            if ((fr % 100U) < 50) {
                                c->blocks[chunk_index_local(lx, y - 1, lz)] = BLOCK_DRIPSTONE_BLOCK;
                                int sm_len = 1 + (int)(fr % 3U);
                                for (int dy = 0; dy < sm_len; dy++) {
                                    int sy = y + dy;
                                    if (sy < h - 4 && c->blocks[chunk_index_local(lx, sy, lz)] == BLOCK_AIR) {
                                        c->blocks[chunk_index_local(lx, sy, lz)] = BLOCK_POINTED_DRIPSTONE;
                                    } else {
                                        break;
                                    }
                                }
                            } else if ((fr % 100U) == 99 && y > 12) {
                                c->blocks[chunk_index_local(lx, y - 1, lz)] = BLOCK_DRIPSTONE_BLOCK;
                                c->blocks[chunk_index_local(lx, y, lz)] = BLOCK_WATER;
                            }
                        }
                    }
                } else if (ubiome == BIOME_DEEP_DARK && y < 28) {
                    /* Deep dark: sculk floors, sensors, catalysts, shriekers */
                    if (cur == BLOCK_AIR && y > 1) {
                        uint8_t below = c->blocks[chunk_index_local(lx, y - 1, lz)];
                        if (below == BLOCK_DEEPSLATE || below == BLOCK_COBBLED_DEEPSLATE) {
                            c->blocks[chunk_index_local(lx, y - 1, lz)] = BLOCK_SCULK;
                            uint32_t sr = mced_hash_u32((uint32_t)wx * 97U ^ (uint32_t)wz * 53U ^ (uint32_t)y);
                            if ((sr % 40U) == 0) {
                                c->blocks[idx] = BLOCK_SCULK_CATALYST;
                            } else if ((sr % 30U) == 1) {
                                c->blocks[idx] = BLOCK_SCULK_SENSOR;
                            } else if ((sr % 35U) == 2) {
                                c->blocks[idx] = BLOCK_SCULK_SHRIEKER;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Amethyst Geodes in deepzone / cave cavities */
    uint32_t geode_seed = mced_hash_u32((uint32_t)c->cx * 0x8DA6B343u ^ (uint32_t)c->cz * 0xD8163841u ^ (uint32_t)w->seed);
    if ((geode_seed % 28U) == 0) {
        int gx = 4 + (int)((geode_seed >> 4) % 8U);
        int gz = 4 + (int)((geode_seed >> 8) % 8U);
        int gy = 16 + (int)((geode_seed >> 12) % 24U);
        place_amethyst_geode(c, gx, gy, gz, w->seed);
    }

    /* 26.3 Ore Generation: Deepslate ores in deep zone, stone ores above, Badlands gold */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = c->heightmap[lx + lz * CHUNK_SIZE];
            Biome biome = biome_at(wx, wz, h, w->seed);
            bool is_badlands = (biome == BIOME_BADLANDS || biome == BIOME_ERODED_BADLANDS || biome == BIOME_WOODED_BADLANDS);
            bool is_mountain = (biome == BIOME_JAGGED_PEAKS || biome == BIOME_FROZEN_PEAKS ||
                                biome == BIOME_STONY_PEAKS || biome == BIOME_MEADOW ||
                                biome == BIOME_CHERRY_GROVE || biome == BIOME_GROVE ||
                                biome == BIOME_SNOWY_SLOPES);

            for (int y = 2; y <= h; y++) {
                int idx = chunk_index_local(lx, y, lz);
                uint8_t b = c->blocks[idx];
                if (b != BLOCK_STONE && b != BLOCK_DEEPSLATE && b != BLOCK_TERRACOTTA &&
                    b != BLOCK_RED_TERRACOTTA && b != BLOCK_ORANGE_TERRACOTTA &&
                    b != BLOCK_YELLOW_TERRACOTTA && b != BLOCK_BROWN_TERRACOTTA &&
                    b != BLOCK_WHITE_TERRACOTTA && b != BLOCK_LIGHT_GRAY_TERRACOTTA)
                    continue;
                uint32_t r = mced_hash_u32((uint32_t)wx * 0x9e3779b1U ^
                                           (uint32_t)wz * 0x85ebca6bU ^
                                           (uint32_t)y * 0xc2b2ae35U ^
                                           (uint32_t)w->seed);
                uint32_t roll = r % 10000U;

                /* Badlands massive gold ore generation up to surface */
                if (is_badlands && y >= 32 && roll < 220) {
                    c->blocks[idx] = BLOCK_GOLD_ORE;
                    continue;
                }

                if (y < 32) {
                    /* Deepslate zone ores */
                    if (y < 16 && roll < 18)
                        c->blocks[idx] = BLOCK_DEEPSLATE_DIAMOND_ORE;
                    else if (y < 24 && roll < 48)
                        c->blocks[idx] = BLOCK_DEEPSLATE_REDSTONE_ORE;
                    else if (roll < 70)
                        c->blocks[idx] = BLOCK_DEEPSLATE_LAPIS_ORE;
                    else if (roll < 100)
                        c->blocks[idx] = BLOCK_DEEPSLATE_GOLD_ORE;
                    else if (roll < 200)
                        c->blocks[idx] = BLOCK_DEEPSLATE_IRON_ORE;
                    else if (roll < 280)
                        c->blocks[idx] = BLOCK_DEEPSLATE_COPPER_ORE;
                    else if (roll < 320)
                        c->blocks[idx] = BLOCK_DEEPSLATE_COAL_ORE;
                    else if (roll < 450)
                        c->blocks[idx] = BLOCK_TUFF;
                    else if (roll < 460)
                        c->blocks[idx] = BLOCK_AMETHYST_BLOCK;
                } else {
                    /* Stone zone ores */
                    if (y < 48 && roll < 70)
                        c->blocks[idx] = BLOCK_GOLD_ORE;
                    else if (y < 56 && roll < 90)
                        c->blocks[idx] = BLOCK_LAPIS_ORE;
                    else if (is_mountain && y > 90 && roll < 150)
                        c->blocks[idx] = BLOCK_EMERALD_ORE;
                    else if (y < 160 && roll < 240)
                        c->blocks[idx] = BLOCK_IRON_ORE;
                    else if (y < 95 && roll < 350)
                        c->blocks[idx] = BLOCK_COPPER_ORE;
                    else if (roll < 520)
                        c->blocks[idx] = BLOCK_COAL_ORE;
                }
            }
        }
    }

    /* Underwater Flora, Corals & Kelp Pass */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = c->cx * CHUNK_SIZE + lx;
            int wz = c->cz * CHUNK_SIZE + lz;
            int h = c->heightmap[lx + lz * CHUNK_SIZE];
            if (h >= SEA_LEVEL)
                continue;
            int depth = SEA_LEVEL - h;
            Biome biome = biome_at(wx, wz, h, w->seed);
            float temp = get_temperature(wx, wz, w->seed);
            uint32_t wr = mced_hash_u32((uint32_t)wx * 0x31415926U ^ (uint32_t)wz * 0x27182818U ^ (uint32_t)w->seed);

            /* 1. Warm Ocean Coral Reefs */
            if ((biome == BIOME_OCEAN || biome == BIOME_DEEP_OCEAN) && temp > 0.15f) {
                float reef = fbm2(wx * 0.05f, wz * 0.05f, w->seed ^ 0x90210ULL, 2);
                if (reef > 0.18f && depth >= 3) {
                    int cheight = 1 + (int)((reef - 0.18f) * 8.0f);
                    if (cheight > depth - 2) cheight = depth - 2;
                    uint32_t ctype = (mced_hash_u32((uint32_t)(wx / 4) * 37U ^ (uint32_t)(wz / 4) * 59U)) % 5U;
                    uint8_t cblock = (uint8_t)(BLOCK_TUBE_CORAL_BLOCK + ctype);
                    uint8_t cplant = (uint8_t)(BLOCK_TUBE_CORAL + ctype);
                    for (int dy = 1; dy <= cheight; dy++) {
                        c->blocks[chunk_index_local(lx, h + dy, lz)] = cblock;
                    }
                    if (h + cheight + 1 < SEA_LEVEL) {
                        c->blocks[chunk_index_local(lx, h + cheight + 1, lz)] = cplant;
                    }
                } else if ((wr % 100U) < 45) {
                    uint32_t flor = (wr / 100U) % 100U;
                    uint32_t ctype = (wr >> 4) % 5U;
                    uint8_t cplant = (uint8_t)(BLOCK_TUBE_CORAL + ctype);
                    if (flor < 50 && h + 1 < SEA_LEVEL) {
                        c->blocks[chunk_index_local(lx, h + 1, lz)] = cplant;
                    } else if (h + 1 < SEA_LEVEL) {
                        c->blocks[chunk_index_local(lx, h + 1, lz)] = BLOCK_SEAGRASS;
                    }
                }
            } else if ((biome == BIOME_OCEAN || biome == BIOME_DEEP_OCEAN) && temp <= 0.15f && depth >= 5) {
                /* 2. Temperate & Cold Ocean Kelp Forests */
                float kelp_patch = noise2(wx * 0.045f, wz * 0.045f, w->seed ^ 0x55AA1122ULL);
                if (kelp_patch > 0.10f && (wr % 100U) < 65) {
                    int kelp_len = 3 + (int)(wr % 9U);
                    if (h + kelp_len >= SEA_LEVEL) kelp_len = SEA_LEVEL - 1 - h;
                    for (int dy = 1; dy <= kelp_len; dy++) {
                        c->blocks[chunk_index_local(lx, h + dy, lz)] = BLOCK_KELP;
                    }
                } else if ((wr % 100U) < 40 && h + 1 < SEA_LEVEL) {
                    c->blocks[chunk_index_local(lx, h + 1, lz)] = BLOCK_SEAGRASS;
                }
            } else {
                /* 3. General Inland Water Seagrass */
                if ((wr % 100U) < 38 && h + 1 < SEA_LEVEL) {
                    c->blocks[chunk_index_local(lx, h + 1, lz)] = BLOCK_SEAGRASS;
                }
            }
        }
    }

    /* 26.3 Vegetation & Biome Decoration */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
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
            bool can_tree = (lx >= 2 && lx < CHUNK_SIZE - 2 && lz >= 2 && lz < CHUNK_SIZE - 2);

            if (biome == BIOME_ICE_SPIKES && top == BLOCK_SNOW_BLOCK) {
                if (can_tree && (r % 75U) == 0) {
                    place_ice_spike(c, lx, lz, h, w->seed);
                }
            } else if (biome == BIOME_CHERRY_GROVE && top == BLOCK_GRASS_BLOCK) {
                if (can_tree && (r % 45U) == 0) {
                    place_cherry_tree(w, c, lx, lz, h, w->seed);
                } else if ((r % 100U) < 75) {
                    uint32_t flor = (r / 100U) % 100U;
                    if (flor < 40) set_local(c, lx, h + 1, lz, BLOCK_PINK_TULIP);
                    else if (flor < 60) set_local(c, lx, h + 1, lz, BLOCK_DANDELION);
                    else if (flor < 75) set_local(c, lx, h + 1, lz, BLOCK_POPPY);
                    else set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                }
            } else if (biome == BIOME_PALE_GARDEN && top == BLOCK_PALE_MOSS_BLOCK) {
                if (can_tree && (r % 45U) == 0) {
                    place_pale_tree(w, c, lx, lz, h, w->seed);
                } else if ((r % 100U) < 25) {
                    set_local(c, lx, h + 1, lz, BLOCK_PALE_HANGING_MOSS);
                }
            } else if (biome == BIOME_OLD_GROWTH_BIRCH_FOREST && top == BLOCK_GRASS_BLOCK) {
                if (can_tree && (r % 30U) == 0) {
                    place_tall_birch(c, lx, lz, h, w->seed);
                } else if ((r % 100U) < 65) {
                    uint32_t flor = (r / 100U) % 100U;
                    if (flor < 65) set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                    else if (flor < 85) set_local(c, lx, h + 1, lz, BLOCK_FERN);
                    else set_local(c, lx, h + 1, lz, BLOCK_WHITE_TULIP);
                }
            } else if ((biome == BIOME_OLD_GROWTH_PINE_TAIGA || biome == BIOME_OLD_GROWTH_SPRUCE_TAIGA) &&
                       (top == BLOCK_PODZOL || top == BLOCK_COARSE_DIRT || top == BLOCK_GRASS_BLOCK)) {
                if (can_tree && (r % 28U) == 0) {
                    place_mega_spruce(c, lx, lz, h, w->seed);
                } else if ((r % 100U) < 60) {
                    uint32_t flor = (r / 100U) % 100U;
                    if (flor < 55) set_local(c, lx, h + 1, lz, BLOCK_FERN);
                    else set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                }
            } else if ((biome == BIOME_FLOWER_FOREST || biome == BIOME_SUNFLOWER_PLAINS) && top == BLOCK_GRASS_BLOCK) {
                if (can_tree && (r % 70U) == 0) {
                    place_tree(w, c, lx, lz, h, w->seed);
                } else if ((r % 100U) < 85) {
                    uint32_t flor = (r / 100U) % 100U;
                    if (flor < 75) set_local(c, lx, h + 1, lz, pick_meadow_flower(flor));
                    else set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                }
            } else if (biome == BIOME_MEADOW && top == BLOCK_GRASS_BLOCK) {
                if (can_tree && (r % 300U) == 0) {
                    place_tree(w, c, lx, lz, h, w->seed);
                } else if ((r % 100U) < 80) {
                    uint32_t flor = (r / 100U) % 100U;
                    if (flor < 65) set_local(c, lx, h + 1, lz, pick_meadow_flower(flor));
                    else set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                }
            } else if (top == BLOCK_GRASS_BLOCK) {
                int chance = (biome == BIOME_FOREST || biome == BIOME_BIRCH_FOREST ||
                              biome == BIOME_DARK_FOREST || biome == BIOME_TAIGA ||
                              biome == BIOME_DAPPLED_FOREST) ? 32 : (biome == BIOME_PLAINS ? 180 : 0);
                if (can_tree && chance && (int)(r % (uint32_t)chance) == 0) {
                    place_tree(w, c, lx, lz, h, w->seed);
                } else if ((r & 0x3ff) == 0) {
                    set_local(c, lx, h + 1, lz, BLOCK_PUMPKIN);
                } else {
                    int cov = (biome == BIOME_PLAINS) ? 75 : 65;
                    if ((int)(r % 100U) < cov) {
                        uint32_t flor = (r / 100U) % 100U;
                        if (biome == BIOME_TAIGA) {
                            if (flor < 50) set_local(c, lx, h + 1, lz, BLOCK_FERN);
                            else set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                        } else if (biome == BIOME_FOREST || biome == BIOME_BIRCH_FOREST || biome == BIOME_DARK_FOREST) {
                            if (flor < 70) set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                            else if (flor < 85) set_local(c, lx, h + 1, lz, BLOCK_FERN);
                            else set_local(c, lx, h + 1, lz, pick_plains_flower(flor));
                        } else {
                            /* Plains */
                            if (flor < 72)
                                set_local(c, lx, h + 1, lz, BLOCK_SHORT_GRASS);
                            else
                                set_local(c, lx, h + 1, lz, pick_plains_flower(flor));
                        }
                    }
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
    if ((n = world_find_chunk(w, c->cx - 1, c->cz, false))) { n->dirty = true; n->version++; }
    if ((n = world_find_chunk(w, c->cx + 1, c->cz, false))) { n->dirty = true; n->version++; }
    if ((n = world_find_chunk(w, c->cx, c->cz - 1, false))) { n->dirty = true; n->version++; }
    if ((n = world_find_chunk(w, c->cx, c->cz + 1, false))) { n->dirty = true; n->version++; }
}

static void world_generate_chunk(World *w, Chunk *c) {
    world_generate_chunk_for_edit(w, c);
}

typedef struct {
    World *w;
    Chunk *c;
} ChunkGenJob;

static void chunk_gen_worker(void *arg) {
    ChunkGenJob *job = (ChunkGenJob *)arg;
    World *w = job->w;
    Chunk *c = job->c;
    world_generate_chunk_for_edit(w, c);
    c->generating = false;
    c->version++;
    free(job);
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
                if (!c->generated && !c->generating) {
                    if (w->pool) {
                        c->generating = true;
                        ChunkGenJob *job = malloc(sizeof(ChunkGenJob));
                        job->w = w;
                        job->c = c;
                        thread_pool_submit(w->pool, chunk_gen_worker, job);
                    } else {
                        world_generate_chunk(w, c);
                    }
                }
            }
        }
    }
    if (w->pool) {
        thread_pool_wait_idle(w->pool);
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
    bool any_missing = false;
    int submitted = 0;
    for (int r = 0; r <= radius; r++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                if (r > 0 && abs(dx) != r && abs(dz) != r)
                    continue;
                int cx = pcx + dx;
                int cz = pcz + dz;
                Chunk *c = world_find_chunk(w, cx, cz, false);
                if (!c || !c->generated) {
                    any_missing = true;
                    if (!c) {
                        c = world_find_chunk(w, cx, cz, true);
                    }
                    if (!c->generated && !c->generating) {
                        if (w->pool) {
                            c->generating = true;
                            ChunkGenJob *job = malloc(sizeof(ChunkGenJob));
                            job->w = w;
                            job->c = c;
                            thread_pool_submit(w->pool, chunk_gen_worker, job);
                            submitted++;
                            if (submitted >= 16) {
                                goto update_done;
                            }
                        } else {
                            world_generate_chunk(w, c);
                            submitted++;
                            if (submitted >= 4) {
                                goto update_done;
                            }
                        }
                    }
                }
            }
        }
    }
update_done:;
    if (!any_missing && submitted == 0) {
        w->fully_loaded = true;
    }
    world_tick_liquids(w);
}

uint8_t world_get_fluid_dist(World *w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return 0;
    int cx = floor_div16(x);
    int cz = floor_div16(z);
    Chunk *c = world_find_chunk(w, cx, cz, false);
    if (!c || !c->generated)
        return 0;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return c->fluid_dist[chunk_index_local(lx, y, lz)];
}

void world_set_fluid_dist(World *w, int x, int y, int z, uint8_t dist) {
    if (y < 0 || y >= WORLD_HEIGHT)
        return;
    int cx = floor_div16(x);
    int cz = floor_div16(z);
    Chunk *c = world_get_chunk(w, cx, cz, true);
    if (!c || !c->generated)
        return;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    c->fluid_dist[chunk_index_local(lx, y, lz)] = dist;
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
    int budget = 96;
    static const int hdx[4] = {1, -1, 0, 0};
    static const int hdz[4] = {0, 0, 1, -1};
    static int lava_delay = 0;
    bool tick_lava = (++lava_delay % 3 == 0);

    while (w->liquid_queue_head != w->liquid_queue_tail && budget-- > 0) {
        LiquidCoord coord = w->liquid_queue[w->liquid_queue_head];
        w->liquid_queue_head = (w->liquid_queue_head + 1) % MAX_LIQUID_TICKS;

        int x = coord.x;
        int y = coord.y;
        int z = coord.z;
        if (y < 0 || y >= WORLD_HEIGHT)
            continue;

        uint8_t cur = world_get_block(w, x, y, z);
        uint8_t cur_dist = world_get_fluid_dist(w, x, y, z);

        /* 1. Infinite water source regeneration (Air block with >= 2 water source neighbors and solid floor) */
        if (cur == BLOCK_AIR) {
            uint8_t under = (y > 0) ? world_get_block(w, x, y - 1, z) : BLOCK_BEDROCK;
            if (under != BLOCK_AIR && under != BLOCK_WATER) {
                int water_sources = 0;
                for (int i = 0; i < 4; i++) {
                    if (world_get_block(w, x + hdx[i], y, z + hdz[i]) == BLOCK_WATER &&
                        world_get_fluid_dist(w, x + hdx[i], y, z + hdz[i]) == 0) {
                        water_sources++;
                    }
                }
                if (water_sources >= 2) {
                    world_set_block(w, x, y, z, BLOCK_WATER, true);
                    world_set_fluid_dist(w, x, y, z, 0);
                    cur = BLOCK_WATER;
                    cur_dist = 0;
                    for (int i = 0; i < 4; i++)
                        world_schedule_liquid_tick(w, x + hdx[i], y, z + hdz[i]);
                    world_schedule_liquid_tick(w, x, y - 1, z);
                }
            }
        }

        /* 2. Water drying up when disconnected from upstream source */
        if (cur == BLOCK_WATER && cur_dist > 0) {
            bool has_upstream = false;
            if (y < WORLD_HEIGHT - 1 && world_get_block(w, x, y + 1, z) == BLOCK_WATER) {
                has_upstream = true;
            } else {
                for (int i = 0; i < 4; i++) {
                    int nx = x + hdx[i], nz = z + hdz[i];
                    if (world_get_block(w, nx, y, nz) == BLOCK_WATER &&
                        world_get_fluid_dist(w, nx, y, nz) < cur_dist) {
                        has_upstream = true;
                        break;
                    }
                }
            }
            if (!has_upstream) {
                world_set_block(w, x, y, z, BLOCK_AIR, true);
                world_set_fluid_dist(w, x, y, z, 0);
                for (int i = 0; i < 4; i++)
                    world_schedule_liquid_tick(w, x + hdx[i], y, z + hdz[i]);
                if (y > 0) world_schedule_liquid_tick(w, x, y - 1, z);
                continue;
            }
        }

        /* 3. Lava drying up when disconnected */
        if (cur == BLOCK_LAVA && cur_dist > 0) {
            bool has_upstream = false;
            if (y < WORLD_HEIGHT - 1 && world_get_block(w, x, y + 1, z) == BLOCK_LAVA) {
                has_upstream = true;
            } else {
                for (int i = 0; i < 4; i++) {
                    int nx = x + hdx[i], nz = z + hdz[i];
                    if (world_get_block(w, nx, y, nz) == BLOCK_LAVA &&
                        world_get_fluid_dist(w, nx, y, nz) < cur_dist) {
                        has_upstream = true;
                        break;
                    }
                }
            }
            if (!has_upstream) {
                world_set_block(w, x, y, z, BLOCK_AIR, true);
                world_set_fluid_dist(w, x, y, z, 0);
                for (int i = 0; i < 4; i++)
                    world_schedule_liquid_tick(w, x + hdx[i], y, z + hdz[i]);
                if (y > 0) world_schedule_liquid_tick(w, x, y - 1, z);
                continue;
            }
        }

        /* 4. Water flowing downwards and spreading */
        if (cur == BLOCK_WATER) {
            if (y > 0) {
                uint8_t below = world_get_block(w, x, y - 1, z);
                if (below == BLOCK_AIR) {
                    world_set_block(w, x, y - 1, z, BLOCK_WATER, true);
                    world_set_fluid_dist(w, x, y - 1, z, 1);
                    world_schedule_liquid_tick(w, x, y - 1, z);
                    continue;
                } else if (below == BLOCK_LAVA) {
                    uint8_t ldist = world_get_fluid_dist(w, x, y - 1, z);
                    world_set_block(w, x, y - 1, z, (ldist == 0) ? BLOCK_OBSIDIAN : BLOCK_COBBLESTONE, true);
                    world_set_fluid_dist(w, x, y - 1, z, 0);
                    continue;
                }
            }
            /* Horizontal water spread (max 7 blocks from source) */
            if (cur_dist < 7) {
                for (int i = 0; i < 4; i++) {
                    int nx = x + hdx[i];
                    int nz = z + hdz[i];
                    uint8_t nb = world_get_block(w, nx, y, nz);
                    if (nb == BLOCK_LAVA) {
                        uint8_t ldist = world_get_fluid_dist(w, nx, y, nz);
                        world_set_block(w, nx, y, nz, (ldist == 0) ? BLOCK_OBSIDIAN : BLOCK_COBBLESTONE, true);
                        world_set_fluid_dist(w, nx, y, nz, 0);
                    } else if (nb == BLOCK_AIR) {
                        world_set_block(w, nx, y, nz, BLOCK_WATER, true);
                        world_set_fluid_dist(w, nx, y, nz, cur_dist + 1);
                        world_schedule_liquid_tick(w, nx, y, nz);
                    } else if (nb == BLOCK_WATER) {
                        uint8_t ndist = world_get_fluid_dist(w, nx, y, nz);
                        if (ndist > cur_dist + 1) {
                            world_set_fluid_dist(w, nx, y, nz, cur_dist + 1);
                            world_schedule_liquid_tick(w, nx, y, nz);
                        }
                    }
                }
            }
        }

        /* 5. Lava flowing downwards and spreading */
        else if (cur == BLOCK_LAVA && tick_lava) {
            if (y > 0) {
                uint8_t below = world_get_block(w, x, y - 1, z);
                if (below == BLOCK_AIR) {
                    world_set_block(w, x, y - 1, z, BLOCK_LAVA, true);
                    world_set_fluid_dist(w, x, y - 1, z, 1);
                    world_schedule_liquid_tick(w, x, y - 1, z);
                    continue;
                } else if (below == BLOCK_WATER) {
                    world_set_block(w, x, y - 1, z, BLOCK_STONE, true);
                    world_set_fluid_dist(w, x, y - 1, z, 0);
                    continue;
                }
            }
            /* Horizontal lava spread (max 3 blocks from source in overworld) */
            if (cur_dist < 3) {
                for (int i = 0; i < 4; i++) {
                    int nx = x + hdx[i];
                    int nz = z + hdz[i];
                    uint8_t nb = world_get_block(w, nx, y, nz);
                    if (nb == BLOCK_WATER) {
                        world_set_block(w, nx, y, nz, BLOCK_COBBLESTONE, true);
                        world_set_fluid_dist(w, nx, y, nz, 0);
                    } else if (nb == BLOCK_AIR) {
                        world_set_block(w, nx, y, nz, BLOCK_LAVA, true);
                        world_set_fluid_dist(w, nx, y, nz, cur_dist + 1);
                        world_schedule_liquid_tick(w, nx, y, nz);
                    } else if (nb == BLOCK_LAVA) {
                        uint8_t ndist = world_get_fluid_dist(w, nx, y, nz);
                        if (ndist > cur_dist + 1) {
                            world_set_fluid_dist(w, nx, y, nz, cur_dist + 1);
                            world_schedule_liquid_tick(w, nx, y, nz);
                        }
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

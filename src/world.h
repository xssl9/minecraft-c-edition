#ifndef MCED_WORLD_H
#define MCED_WORLD_H

#include "blocks.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CHUNK_SIZE 16
#define WORLD_HEIGHT 256
#define SEA_LEVEL 63
#define CHUNK_AREA (CHUNK_SIZE * CHUNK_SIZE)
#define CHUNK_VOLUME (CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT)

typedef struct {
    int cx, cz;
    uint8_t blocks[CHUNK_VOLUME];
    uint16_t heightmap[CHUNK_AREA];
    uint8_t redstone_power[CHUNK_VOLUME];
    uint8_t fluid_dist[CHUNK_VOLUME];
    bool generated;
    bool dirty;
    bool generating;
    uint32_t version;
} Chunk;

typedef struct {
    int32_t x;
    int32_t z;
    int16_t y;
    uint8_t block;
} BlockMod;

#define MAX_LIQUID_TICKS 4096
typedef struct {
    int32_t x;
    int16_t y;
    int32_t z;
} LiquidCoord;

struct ThreadPool;

typedef struct {
    uint64_t seed;
    Chunk **chunks;
    int chunk_count;
    int chunk_capacity;
    Chunk **chunk_hash;
    int chunk_hash_capacity;
    BlockMod *mods;
    int mod_count;
    int mod_capacity;
    int load_radius;
    bool dirty;
    int last_pcx, last_pcz;
    bool fully_loaded;
    LiquidCoord liquid_queue[MAX_LIQUID_TICKS];
    int liquid_queue_head;
    int liquid_queue_tail;
    struct ThreadPool *pool;
    void *chunk_mutex;
} World;

void world_init(World *w, uint64_t seed, int load_radius);
void world_destroy(World *w);
void world_set_thread_pool(World *w, struct ThreadPool *pool);

Chunk *world_get_chunk(World *w, int cx, int cz, bool create);
Chunk *world_find_chunk(World *w, int cx, int cz, bool create);

uint8_t world_get_block(World *w, int x, int y, int z);
void world_set_block(World *w, int x, int y, int z, uint8_t block, bool record);
void world_set_block_raw(World *w, int x, int y, int z, uint8_t block);

void world_schedule_liquid_tick(World *w, int x, int y, int z);
void world_tick_liquids(World *w);
uint8_t world_get_fluid_dist(World *w, int x, int y, int z);
void world_set_fluid_dist(World *w, int x, int y, int z, uint8_t dist);

uint8_t world_get_power(World *w, int x, int y, int z);
void world_set_power(World *w, int x, int y, int z, uint8_t power);
void world_update_redstone_at(World *w, int x, int y, int z);

void world_update(World *w, double player_x, double player_z);
void world_preload(World *w, double player_x, double player_z, int radius);
int world_height_at(World *w, int x, int z);
bool world_find_spawn(World *w, int *out_x, int *out_y, int *out_z);

bool world_write(World *w, FILE *f);
bool world_read(World *w, FILE *f);
bool world_probe_seed(const char *path, uint64_t *seed, bool *has_save);

static inline int chunk_index_local(int x, int y, int z) {
    return x + z * CHUNK_SIZE + y * CHUNK_AREA;
}

static inline uint32_t chunk_hash_key(int cx, int cz) {
    uint32_t x = (uint32_t)cx * 0x8DA6B343u;
    uint32_t z = (uint32_t)cz * 0xD8163841u;
    return x ^ z;
}

#endif

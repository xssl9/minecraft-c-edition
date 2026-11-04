#ifndef MCED_ASSETS_H
#define MCED_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

#define MCED_ATLAS_SIZE 1024
#define MCED_ATLAS_TILE 16
#define MCED_ATLAS_TILES (MCED_ATLAS_SIZE / MCED_ATLAS_TILE)

typedef struct {
    char name[80];
    uint16_t tile;
} AssetTile;

typedef struct {
    char root[512];
    uint8_t *atlas_rgba;
    uint8_t *atlas_rgb;
    int atlas_size;
    AssetTile *tiles;
    int tile_count;
    int tile_capacity;

    uint8_t *menu_rgb;
    int menu_size;
    uint8_t *logo_rgb;
    int logo_size;

    uint8_t font_alpha[128 * 128];
    bool has_font;
    bool has_menu;
    bool has_logo;
} Assets;

bool assets_load(Assets *a, const char *root);
void assets_free(Assets *a);

int assets_tile(const Assets *a, const char *name);
void assets_tile_uv(const Assets *a, int tile, float *u0, float *v0, float *u1, float *v1);

#endif

#include "assets.h"

#include "mced.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *data = xmalloc((size_t)n + 1);
    size_t got = fread(data, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(data);
        return NULL;
    }
    data[n] = 0;
    if (size_out)
        *size_out = (size_t)n;
    return data;
}

static bool readable_dir(const char *path) {
    return access(path, R_OK) == 0;
}

static bool make_asset_root(char *out, size_t out_size, const char *hint) {
    const char *env = getenv("MCED_ASSET_DIR");
    if (env && readable_dir(env)) {
        snprintf(out, out_size, "%s", env);
        return true;
    }

    char candidates[8][640];
    int n = 0;
    /* Prefer assets next to the current directory.  When the binary lives
       in build/ the hint would otherwise win and put saves in build/saves
       even though the user launched the game from the project root. */
    snprintf(candidates[n++], sizeof(candidates[0]), "./assets");
    snprintf(candidates[n++], sizeof(candidates[0]), "assets");
    if (hint && hint[0]) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", hint);
        char *slash = strrchr(buf, '/');
        if (slash)
            *slash = '\0';
        else
            snprintf(buf, sizeof(buf), ".");
        snprintf(candidates[n++], sizeof(candidates[0]), "%s/assets", buf);
        snprintf(candidates[n++], sizeof(candidates[0]), "%s/../assets", buf);
    }
    snprintf(candidates[n++], sizeof(candidates[0]), "../assets");
    snprintf(candidates[n++], sizeof(candidates[0]), "../../assets");
    snprintf(candidates[n++], sizeof(candidates[0]), "../../../assets");

    for (int i = 0; i < n; i++) {
        if (readable_dir(candidates[i])) {
            size_t len = strlen(candidates[i]);
            if (len >= out_size) len = out_size - 1;
            memcpy(out, candidates[i], len);
            out[len] = '\0';
            return true;
        }
    }
    return false;
}

void assets_add_tile(Assets *a, const char *name, int tile) {
    if (a->tile_count == a->tile_capacity) {
        a->tile_capacity = a->tile_capacity ? a->tile_capacity * 2 : 2048;
        a->tiles = xrealloc(a->tiles, (size_t)a->tile_capacity * sizeof(AssetTile));
    }
    AssetTile *t = &a->tiles[a->tile_count++];
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';
    t->tile = (uint16_t)tile;
}

bool assets_load(Assets *a, const char *hint) {
    memset(a, 0, sizeof(*a));
    a->atlas_size = MCED_ATLAS_SIZE;
    if (!make_asset_root(a->root, sizeof(a->root), hint)) {
        fprintf(stderr, "could not find asset directory (expected ./assets)\n");
        return false;
    }

    char path[768];
    size_t n = 0;
    snprintf(path, sizeof(path), "%s/generated/world_atlas.rgba", a->root);
    a->atlas_rgba = read_file(path, &n);
    if (!a->atlas_rgba || n != (size_t)(MCED_ATLAS_SIZE * MCED_ATLAS_SIZE * 4)) {
        free(a->atlas_rgba);
        a->atlas_rgba = NULL;
    }

    snprintf(path, sizeof(path), "%s/generated/world_atlas.rgb", a->root);
    a->atlas_rgb = read_file(path, &n);
    if (!a->atlas_rgba && (!a->atlas_rgb || n != (size_t)(MCED_ATLAS_SIZE * MCED_ATLAS_SIZE * 3))) {
        fprintf(stderr, "missing/invalid %s\n", path);
        fprintf(stderr, "assets directory is missing or incomplete (expected ./assets)\n");
        return false;
    }

    snprintf(path, sizeof(path), "%s/generated/world_atlas.txt", a->root);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "missing %s\n", path);
        return false;
    }
    char name[96];
    int tile;
    while (fscanf(f, "%95s %d", name, &tile) == 2)
        assets_add_tile(a, name, tile);
    fclose(f);

    snprintf(path, sizeof(path), "%s/generated/menu.rgb", a->root);
    a->menu_rgb = read_file(path, &n);
    if (a->menu_rgb && n == (size_t)(MCED_ATLAS_SIZE * MCED_ATLAS_SIZE * 3)) {
        a->menu_size = MCED_ATLAS_SIZE;
        a->has_menu = true;
    } else {
        free(a->menu_rgb);
        a->menu_rgb = NULL;
    }

    snprintf(path, sizeof(path), "%s/generated/logo.rgb", a->root);
    a->logo_rgb = read_file(path, &n);
    if (a->logo_rgb && n == (size_t)(MCED_ATLAS_SIZE * MCED_ATLAS_SIZE * 3)) {
        a->logo_size = MCED_ATLAS_SIZE;
        a->has_logo = true;
    } else {
        free(a->logo_rgb);
        a->logo_rgb = NULL;
    }

    snprintf(path, sizeof(path), "%s/generated/font_ascii.alpha", a->root);
    FILE *ff = fopen(path, "rb");
    if (ff) {
        a->has_font = fread(a->font_alpha, 1, sizeof(a->font_alpha), ff) == sizeof(a->font_alpha);
        fclose(ff);
    }

    printf("assets: %s (%d texture tiles, menu=%d, font=%d)\n", a->root,
           a->tile_count, a->has_menu, a->has_font);
    return a->tile_count > 0;
}

void assets_free(Assets *a) {
    free(a->atlas_rgba);
    free(a->atlas_rgb);
    free(a->menu_rgb);
    free(a->logo_rgb);
    free(a->tiles);
    memset(a, 0, sizeof(*a));
}

int assets_tile(const Assets *a, const char *name) {
    for (int i = 0; i < a->tile_count; i++) {
        if (strcmp(a->tiles[i].name, name) == 0)
            return a->tiles[i].tile;
    }
    return -1;
}

void assets_tile_uv(const Assets *a, int tile, float *u0, float *v0, float *u1, float *v1) {
    if (tile < 0)
        tile = 0;
    int tx = (tile % MCED_ATLAS_TILES) * MCED_ATLAS_TILE;
    int ty = (tile / MCED_ATLAS_TILES) * MCED_ATLAS_TILE;
    const float inset = 0.5f;
    *u0 = (float)(tx + inset) / (float)a->atlas_size;
    *v0 = (float)(ty + inset) / (float)a->atlas_size;
    *u1 = (float)(tx + MCED_ATLAS_TILE - inset) / (float)a->atlas_size;
    *v1 = (float)(ty + MCED_ATLAS_TILE - inset) / (float)a->atlas_size;
}

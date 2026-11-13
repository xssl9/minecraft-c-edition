#ifndef MCED_DUNIT_H
#define MCED_DUNIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct McedDunit McedDunit;

McedDunit* mced_dunit_create(int width, int height, const char *asset_hint);
McedDunit* mced_dunit_create_embedded(
    int width, int height,
    const uint8_t *atlas_png, size_t atlas_png_len,
    const char *atlas_txt, size_t atlas_txt_len,
    const uint8_t *font_alpha, size_t font_alpha_len
);
void mced_dunit_destroy(McedDunit *d);

void mced_dunit_update(McedDunit *d, float dt);
void mced_dunit_resize(McedDunit *d, int width, int height);
const uint32_t* mced_dunit_render(McedDunit *d);

void mced_dunit_key(McedDunit *d, int scancode, int key_char, bool down);
void mced_dunit_mouse_move(McedDunit *d, int x, int y);
void mced_dunit_mouse_leave(McedDunit *d);
void mced_dunit_mouse_look(McedDunit *d, float dx, float dy);
void mced_dunit_mouse_button(McedDunit *d, int button, bool down);
void mced_dunit_mouse_scroll(McedDunit *d, int delta);

void mced_dunit_start_world(McedDunit *d, bool new_world);
bool mced_dunit_is_in_world(McedDunit *d);

#ifdef __cplusplus
}
#endif

#endif

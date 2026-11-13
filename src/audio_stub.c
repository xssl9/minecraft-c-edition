#include "audio.h"
#include <string.h>

bool audio_init(Audio *a, const Assets *assets) {
    (void)assets;
    if (a) memset(a, 0, sizeof(*a));
    return true;
}

void audio_shutdown(Audio *a) {
    if (a) memset(a, 0, sizeof(*a));
}

void audio_set_enabled(Audio *a, bool enabled) {
    if (a) a->enabled = enabled;
}

void audio_set_master_volume(Audio *a, float vol) { (void)a; (void)vol; }
void audio_set_music_volume(Audio *a, float vol) { (void)a; (void)vol; }
void audio_set_sound_volume(Audio *a, float vol) { (void)a; (void)vol; }
void audio_set_blocks_volume(Audio *a, float vol) { (void)a; (void)vol; }
void audio_play_click(Audio *a) { (void)a; }
void audio_play_break(Audio *a, uint8_t block) { (void)a; (void)block; }
void audio_play_place(Audio *a, uint8_t block) { (void)a; (void)block; }
void audio_play_step(Audio *a, uint8_t block) { (void)a; (void)block; }
void audio_play_dig(Audio *a, uint8_t block) { (void)a; (void)block; }
void audio_play_hurt(Audio *a) { (void)a; }
void audio_play_splash(Audio *a) { (void)a; }
void audio_play_pop(Audio *a) { (void)a; }
void audio_play_explode(Audio *a) { (void)a; }
void audio_play_fuse(Audio *a) { (void)a; }
void audio_music_menu(Audio *a, bool on) { (void)a; (void)on; }
void audio_play_cave(Audio *a) { (void)a; }
void audio_play_fizz(Audio *a) { (void)a; }
void audio_play_eat(Audio *a) { (void)a; }
void audio_play_chest_open(Audio *a) { (void)a; }
void audio_play_chest_close(Audio *a) { (void)a; }
void audio_play_door_open(Audio *a) { (void)a; }
void audio_play_door_close(Audio *a) { (void)a; }
void audio_play_bow(Audio *a) { (void)a; }
void audio_play_bow_hit(Audio *a) { (void)a; }


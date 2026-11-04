#ifndef MCED_AUDIO_H
#define MCED_AUDIO_H

#include "assets.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Sound Sound;

#define AUDIO_VOICES 16

#define MAT_COUNT 8
typedef enum {
    MAT_STONE = 0,
    MAT_WOOD,
    MAT_GRASS,
    MAT_GRAVEL,
    MAT_SAND,
    MAT_SNOW,
    MAT_GLASS,
    MAT_CLOTH
} AudioMaterial;

typedef struct {
    bool available;
    bool enabled;
    bool music_on;
    void *device;
    Sound *dig[MAT_COUNT][6];
    int dig_count[MAT_COUNT];
    Sound *step[MAT_COUNT][6];
    int step_count[MAT_COUNT];
    Sound *glass[3];
    Sound *hit[3];
    Sound *splash[2];
    Sound *pop_snd;
    Sound *break_snd;
    Sound *click_snd;
    Sound *music_menu;
    Sound *ambient_cave;
    Sound *explode[4];
    Sound *fuse_snd;
    Sound *fizz_snd;
    Sound *eat_snd[3];
    Sound *chest_open_snd;
    Sound *chest_close_snd;
    Sound *door_open_snd;
    Sound *door_close_snd;
    Sound *bow_snd;
    Sound *bow_hit[4];
    void *voices;
    unsigned int rng;

    float master_volume;
    float music_volume;
    float sound_volume;
    float blocks_volume;
} Audio;

bool audio_init(Audio *a, const Assets *assets);
void audio_shutdown(Audio *a);
void audio_set_enabled(Audio *a, bool enabled);
void audio_set_master_volume(Audio *a, float vol);
void audio_set_music_volume(Audio *a, float vol);
void audio_set_sound_volume(Audio *a, float vol);
void audio_set_blocks_volume(Audio *a, float vol);
void audio_play_click(Audio *a);
void audio_play_break(Audio *a, uint8_t block);
void audio_play_place(Audio *a, uint8_t block);
void audio_play_step(Audio *a, uint8_t block);
void audio_play_dig(Audio *a, uint8_t block);
void audio_play_hurt(Audio *a);
void audio_play_splash(Audio *a);
void audio_play_pop(Audio *a);
void audio_play_explode(Audio *a);
void audio_play_fuse(Audio *a);
void audio_music_menu(Audio *a, bool on);
void audio_play_cave(Audio *a);
void audio_play_fizz(Audio *a);
void audio_play_eat(Audio *a);
void audio_play_chest_open(Audio *a);
void audio_play_chest_close(Audio *a);
void audio_play_door_open(Audio *a);
void audio_play_door_close(Audio *a);
void audio_play_bow(Audio *a);
void audio_play_bow_hit(Audio *a);

#endif

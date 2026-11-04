#include "audio.h"

#include "blocks.h"
#include "mced.h"

#include <SDL.h>
#include <math.h>

#ifdef MCED_HAVE_VORBIS
#include <vorbis/vorbisfile.h>
#endif

#define MCED_AUDIO_RATE 44100

struct Sound {
    int16_t *samples;   /* interleaved */
    int frames;
    int channels;
    int rate;
};

typedef struct {
    Sound *sound;
    double pos;         /* frame position in source */
    float gain;
    float pitch;
    bool loop;
    bool active;
} Voice;

#ifdef MCED_HAVE_VORBIS
static Sound *load_ogg(const char *path) {
    OggVorbis_File vf;
    if (ov_fopen(path, &vf) != 0)
        return NULL;
    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        return NULL;
    }
    int channels = vi->channels;
    int rate = (int)vi->rate;
    size_t capacity = 0, count = 0;
    int16_t *samples = NULL;
    int bs = 0;
    for (;;) {
        if (count + 4096 > capacity) {
            capacity = capacity ? capacity * 2 : 65536;
            samples = xrealloc(samples, capacity * sizeof(int16_t));
        }
        long got = ov_read(&vf, (char *)(samples + count),
                           (int)((capacity - count) * sizeof(int16_t)),
                           0, 2, 1, &bs);
        if (got == 0)
            break;
        if (got < 0) {
            free(samples);
            ov_clear(&vf);
            return NULL;
        }
        count += (size_t)got / sizeof(int16_t);
    }
    ov_clear(&vf);
    if (count == 0) {
        free(samples);
        return NULL;
    }
    Sound *s = xcalloc(1, sizeof(Sound));
    s->samples = samples;
    s->channels = channels;
    s->rate = rate;
    s->frames = (int)(count / (size_t)channels);
    return s;
}
#else
static Sound *load_ogg(const char *path) {
    (void)path;
    return NULL;
}
#endif

static void sound_free(Sound *s) {
    if (!s)
        return;
    free(s->samples);
    free(s);
}

static void play_voice_ex(Audio *a, Sound *s, float gain, float pitch, bool loop) {
    if (!a || !a->available || !a->enabled || !s)
        return;
    Voice *voices = (Voice *)a->voices;
    int slot = -1;
    for (int i = 1; i < AUDIO_VOICES; i++) {
        if (!voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* Steal the quietest effect. */
        float q = 1e9f;
        slot = 1;
        for (int i = 1; i < AUDIO_VOICES; i++) {
            if (voices[i].gain < q) {
                q = voices[i].gain;
                slot = i;
            }
        }
    }
    Voice *v = &voices[slot];
    v->sound = s;
    v->pos = 0;
    v->gain = gain;
    v->pitch = (pitch > 0.05f) ? pitch : 1.0f;
    v->loop = loop;
    v->active = true;
}

static void play_voice(Audio *a, Sound *s, float gain, bool loop) {
    play_voice_ex(a, s, gain, 1.0f, loop);
}

static void mix_callback(void *userdata, uint8_t *stream, int len) {
    Audio *a = (Audio *)userdata;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4; /* stereo 16-bit */
    memset(stream, 0, (size_t)len);
    if (!a->enabled)
        return;

    Voice *voices = (Voice *)a->voices;
    for (int vi = 0; vi < AUDIO_VOICES; vi++) {
        Voice *v = &voices[vi];
        if (!v->active || !v->sound)
            continue;
        Sound *s = v->sound;
        float cat_vol = (s == a->music_menu) ? a->music_volume : a->sound_volume;
        float final_gain = v->gain * a->master_volume * cat_vol;
        if (final_gain < 0.0001f)
            continue;
        float pitch = (v->pitch > 0.05f) ? v->pitch : 1.0f;
        double inc = ((double)s->rate / (double)MCED_AUDIO_RATE) * (double)pitch;
        for (int i = 0; i < frames; i++) {
            if (v->pos >= (double)s->frames) {
                if (v->loop)
                    v->pos = fmod(v->pos, (double)s->frames);
                else {
                    v->active = false;
                    break;
                }
            }
            int f = (int)v->pos;
            int f2 = f + 1;
            if (f2 >= s->frames)
                f2 = v->loop ? 0 : f;
            double frac = v->pos - (double)f;
            float l, r;
            if (s->channels == 2) {
                float a0 = s->samples[f * 2 + 0];
                float a1 = s->samples[f2 * 2 + 0];
                float b0 = s->samples[f * 2 + 1];
                float b1 = s->samples[f2 * 2 + 1];
                l = (float)((1 - frac) * a0 + frac * a1);
                r = (float)((1 - frac) * b0 + frac * b1);
            } else {
                float a0 = s->samples[f];
                float a1 = s->samples[f2];
                l = r = (float)((1 - frac) * a0 + frac * a1);
            }
            int32_t il = out[i * 2 + 0] + (int32_t)(l * final_gain);
            int32_t ir = out[i * 2 + 1] + (int32_t)(r * final_gain);
            out[i * 2 + 0] = (int16_t)clampi(il, -32768, 32767);
            out[i * 2 + 1] = (int16_t)clampi(ir, -32768, 32767);
            v->pos += inc;
        }
    }
}

static Sound *load_sound_path(const Assets *assets, const char *rel) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/sounds/minecraft/sounds/%s", assets->root, rel);
    Sound *s = load_ogg(path);
    if (!s)
        fprintf(stderr, "audio: missing %s\n", path);
    return s;
}

bool audio_init(Audio *a, const Assets *assets) {
    memset(a, 0, sizeof(*a));
    a->enabled = true;
    a->rng = 0x1234abcdU;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "audio: SDL audio unavailable: %s\n", SDL_GetError());
        return false;
    }

    a->voices = xcalloc(AUDIO_VOICES, sizeof(Voice));
    a->master_volume = 1.0f;
    a->music_volume = 0.7f;
    a->sound_volume = 1.0f;
    a->blocks_volume = 1.0f;

    /* Load stone (dig 1-4, step 1-6) */
    a->dig_count[MAT_STONE] = 4;
    a->step_count[MAT_STONE] = 6;
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "dig/stone%d.ogg", i + 1);
        a->dig[MAT_STONE][i] = load_sound_path(assets, rel);
    }
    for (int i = 0; i < 6; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "step/stone%d.ogg", i + 1);
        a->step[MAT_STONE][i] = load_sound_path(assets, rel);
    }

    /* Load wood (dig 1-4, step 1-6) */
    a->dig_count[MAT_WOOD] = 4;
    a->step_count[MAT_WOOD] = 6;
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "dig/wood%d.ogg", i + 1);
        a->dig[MAT_WOOD][i] = load_sound_path(assets, rel);
    }
    for (int i = 0; i < 6; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "step/wood%d.ogg", i + 1);
        a->step[MAT_WOOD][i] = load_sound_path(assets, rel);
    }

    /* Load grass (dig 1-4, step 1-6) */
    a->dig_count[MAT_GRASS] = 4;
    a->step_count[MAT_GRASS] = 6;
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "dig/grass%d.ogg", i + 1);
        a->dig[MAT_GRASS][i] = load_sound_path(assets, rel);
    }
    for (int i = 0; i < 6; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "step/grass%d.ogg", i + 1);
        a->step[MAT_GRASS][i] = load_sound_path(assets, rel);
    }

    /* Load gravel (dig 1-4, step 1-4) */
    a->dig_count[MAT_GRAVEL] = 4;
    a->step_count[MAT_GRAVEL] = 4;
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "dig/gravel%d.ogg", i + 1);
        a->dig[MAT_GRAVEL][i] = load_sound_path(assets, rel);
        snprintf(rel, sizeof(rel), "step/gravel%d.ogg", i + 1);
        a->step[MAT_GRAVEL][i] = load_sound_path(assets, rel);
    }

    /* Load sand (dig 1-4, step 1-5) */
    a->dig_count[MAT_SAND] = 4;
    a->step_count[MAT_SAND] = 5;
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "dig/sand%d.ogg", i + 1);
        a->dig[MAT_SAND][i] = load_sound_path(assets, rel);
    }
    for (int i = 0; i < 5; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "step/sand%d.ogg", i + 1);
        a->step[MAT_SAND][i] = load_sound_path(assets, rel);
    }

    /* Load snow (dig 1-4, step 1-4) */
    a->dig_count[MAT_SNOW] = 4;
    a->step_count[MAT_SNOW] = 4;
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "dig/snow%d.ogg", i + 1);
        a->dig[MAT_SNOW][i] = load_sound_path(assets, rel);
        snprintf(rel, sizeof(rel), "step/snow%d.ogg", i + 1);
        a->step[MAT_SNOW][i] = load_sound_path(assets, rel);
    }

    /* Load glass (random/glass 1-3) */
    a->dig_count[MAT_GLASS] = 3;
    a->step_count[MAT_GLASS] = 4;
    for (int i = 0; i < 3; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "random/glass%d.ogg", i + 1);
        a->glass[i] = load_sound_path(assets, rel);
        a->dig[MAT_GLASS][i] = a->glass[i];
    }
    for (int i = 0; i < 4; i++) {
        a->step[MAT_GLASS][i] = a->step[MAT_STONE][i];
    }

    /* Load cloth/wool (dig 1-4, step 1-4) */
    a->dig_count[MAT_CLOTH] = 4;
    a->step_count[MAT_CLOTH] = 4;
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "dig/cloth%d.ogg", i + 1);
        a->dig[MAT_CLOTH][i] = load_sound_path(assets, rel);
        snprintf(rel, sizeof(rel), "step/cloth%d.ogg", i + 1);
        a->step[MAT_CLOTH][i] = load_sound_path(assets, rel);
    }

    /* Damage hit sounds */
    for (int i = 0; i < 3; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "damage/hit%d.ogg", i + 1);
        a->hit[i] = load_sound_path(assets, rel);
    }

    /* Splash sounds */
    a->splash[0] = load_sound_path(assets, "liquid/splash.ogg");
    a->splash[1] = load_sound_path(assets, "liquid/splash2.ogg");

    a->pop_snd = load_sound_path(assets, "random/pop.ogg");
    a->break_snd = load_sound_path(assets, "random/break.ogg");
    a->click_snd = load_sound_path(assets, "random/click.ogg");
    a->fuse_snd = load_sound_path(assets, "random/fuse.ogg");
    a->fizz_snd = load_sound_path(assets, "random/fizz.ogg");
    a->chest_open_snd = load_sound_path(assets, "random/chestopen.ogg");
    a->chest_close_snd = load_sound_path(assets, "random/chestclosed.ogg");
    a->door_open_snd = load_sound_path(assets, "random/door_open.ogg");
    a->door_close_snd = load_sound_path(assets, "random/door_close.ogg");
    a->bow_snd = load_sound_path(assets, "random/bow.ogg");
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "random/bowhit%d.ogg", i + 1);
        a->bow_hit[i] = load_sound_path(assets, rel);
    }
    for (int i = 0; i < 3; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "random/eat%d.ogg", i + 1);
        a->eat_snd[i] = load_sound_path(assets, rel);
    }
    for (int i = 0; i < 4; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "random/explode%d.ogg", i + 1);
        a->explode[i] = load_sound_path(assets, rel);
    }
    a->music_menu = load_sound_path(assets, "music/menu/moog_city_2.ogg");
    a->ambient_cave = load_sound_path(assets, "ambient/cave/cave1.ogg");

    SDL_AudioSpec want;
    memset(&want, 0, sizeof(want));
    want.freq = MCED_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix_callback;
    want.userdata = a;
    SDL_AudioSpec have;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        fprintf(stderr, "audio: could not open device: %s\n", SDL_GetError());
        return false;
    }
    a->device = (void *)(uintptr_t)dev;
    a->available = true;
    SDL_PauseAudioDevice(dev, 0);
    printf("audio: %d Hz stereo authentic Minecraft sound mixer initialized\n", have.freq);
    return true;
}

void audio_shutdown(Audio *a) {
    if (!a)
        return;
    if (a->available) {
        SDL_AudioDeviceID dev = (SDL_AudioDeviceID)(uintptr_t)a->device;
        SDL_CloseAudioDevice(dev);
    }
    if (a->voices) {
        free(a->voices);
        a->voices = NULL;
    }
    for (int k = 0; k < MAT_COUNT; k++) {
        if (k == MAT_GLASS)
            continue; /* freed with a->glass */
        for (int i = 0; i < a->dig_count[k]; i++) {
            sound_free(a->dig[k][i]);
            a->dig[k][i] = NULL;
        }
        for (int i = 0; i < a->step_count[k]; i++) {
            sound_free(a->step[k][i]);
            a->step[k][i] = NULL;
        }
    }
    for (int i = 0; i < 3; i++) {
        sound_free(a->glass[i]);
        a->glass[i] = NULL;
        sound_free(a->hit[i]);
        a->hit[i] = NULL;
    }
    for (int i = 0; i < 2; i++) {
        sound_free(a->splash[i]);
        a->splash[i] = NULL;
    }
    sound_free(a->pop_snd);
    sound_free(a->break_snd);
    sound_free(a->click_snd);
    sound_free(a->fuse_snd);
    sound_free(a->fizz_snd);
    sound_free(a->chest_open_snd);
    sound_free(a->chest_close_snd);
    sound_free(a->door_open_snd);
    sound_free(a->door_close_snd);
    sound_free(a->bow_snd);
    for (int i = 0; i < 4; i++) {
        sound_free(a->bow_hit[i]);
        a->bow_hit[i] = NULL;
    }
    for (int i = 0; i < 3; i++) {
        sound_free(a->eat_snd[i]);
        a->eat_snd[i] = NULL;
    }
    for (int i = 0; i < 4; i++) {
        sound_free(a->explode[i]);
        a->explode[i] = NULL;
    }
    sound_free(a->music_menu);
    sound_free(a->ambient_cave);
    memset(a, 0, sizeof(*a));
}

void audio_set_enabled(Audio *a, bool enabled) {
    if (!a || !a->available)
        return;
    a->enabled = enabled;
    if (!enabled) {
        Voice *voices = (Voice *)a->voices;
        for (int i = 1; i < AUDIO_VOICES; i++)
            voices[i].active = false;
    }
}

void audio_set_master_volume(Audio *a, float vol) {
    if (!a) return;
    a->master_volume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

void audio_set_music_volume(Audio *a, float vol) {
    if (!a) return;
    a->music_volume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

void audio_set_sound_volume(Audio *a, float vol) {
    if (!a) return;
    a->sound_volume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

void audio_set_blocks_volume(Audio *a, float vol) {
    if (!a) return;
    a->blocks_volume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

void audio_play_click(Audio *a) {
    play_voice(a, a->click_snd, 0.55f, false);
}

static int material_for_block(uint8_t block) {
    switch (block) {
    case BLOCK_OAK_LOG: case BLOCK_BIRCH_LOG: case BLOCK_SPRUCE_LOG:
    case BLOCK_JUNGLE_LOG: case BLOCK_ACACIA_LOG: case BLOCK_DARK_OAK_LOG:
    case BLOCK_OAK_PLANKS: case BLOCK_BIRCH_PLANKS: case BLOCK_SPRUCE_PLANKS:
    case BLOCK_JUNGLE_PLANKS: case BLOCK_ACACIA_PLANKS: case BLOCK_DARK_OAK_PLANKS:
    case BLOCK_CRAFTING_TABLE: case BLOCK_BOOKSHELF:
        return MAT_WOOD;

    case BLOCK_GRASS_BLOCK:
    case BLOCK_OAK_LEAVES: case BLOCK_BIRCH_LEAVES: case BLOCK_SPRUCE_LEAVES:
    case BLOCK_JUNGLE_LEAVES: case BLOCK_ACACIA_LEAVES: case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_CACTUS: case BLOCK_PUMPKIN: case BLOCK_CARVED_PUMPKIN:
    case BLOCK_JACK_O_LANTERN: case BLOCK_MELON: case BLOCK_HAY_BLOCK:
    case BLOCK_MOSS_BLOCK:
        return MAT_GRASS;

    case BLOCK_DIRT: case BLOCK_COARSE_DIRT: case BLOCK_PODZOL:
    case BLOCK_MYCELIUM: case BLOCK_GRAVEL: case BLOCK_CLAY:
        return MAT_GRAVEL;

    case BLOCK_SAND: case BLOCK_RED_SAND: case BLOCK_SOUL_SAND:
        return MAT_SAND;

    case BLOCK_SNOW_BLOCK:
        return MAT_SNOW;

    case BLOCK_GLASS: case BLOCK_GLOWSTONE: case BLOCK_SEA_LANTERN:
    case BLOCK_ICE: case BLOCK_PACKED_ICE:
        return MAT_GLASS;

    case BLOCK_WHITE_WOOL: case BLOCK_RED_WOOL: case BLOCK_ORANGE_WOOL:
    case BLOCK_YELLOW_WOOL: case BLOCK_LIME_WOOL: case BLOCK_BLUE_WOOL:
    case BLOCK_PURPLE_WOOL: case BLOCK_BLACK_WOOL: case BLOCK_SPONGE:
        return MAT_CLOTH;

    default:
        return MAT_STONE;
    }
}

static Sound *pick(Audio *a, Sound *arr[], int count) {
    if (!a || count <= 0)
        return NULL;
    a->rng = mced_hash_u32(a->rng + 0x9e3779b9U);
    return arr[a->rng % (unsigned int)count];
}

void audio_play_break(Audio *a, uint8_t block) {
    if (!a) return;
    int m = material_for_block(block);
    /* In Minecraft, block break uses pitch 0.8 * (0.8..1.0) and NO tool snap sound */
    float pitch = 0.75f + (float)(a->rng & 15) * 0.012f;
    if (m == MAT_GLASS) {
        Sound *s = pick(a, a->glass, 3);
        play_voice_ex(a, s, 0.75f, pitch, false);
    } else {
        Sound *s = pick(a, a->dig[m], a->dig_count[m]);
        play_voice_ex(a, s, 0.85f, pitch, false);
    }
}

void audio_play_dig(Audio *a, uint8_t block) {
    if (!a) return;
    int m = material_for_block(block);
    float pitch = 0.65f + (float)(a->rng & 15) * 0.015f;
    if (m == MAT_GLASS) {
        Sound *s = pick(a, a->step[MAT_STONE], a->step_count[MAT_STONE]);
        play_voice_ex(a, s, 0.35f, pitch, false);
    } else {
        Sound *s = pick(a, a->dig[m], a->dig_count[m]);
        play_voice_ex(a, s, 0.40f, pitch, false);
    }
}

void audio_play_place(Audio *a, uint8_t block) {
    if (!a) return;
    int m = material_for_block(block);
    float pitch = 0.80f + (float)(a->rng & 15) * 0.015f;
    if (m == MAT_GLASS) {
        Sound *s = pick(a, a->step[MAT_STONE], a->step_count[MAT_STONE]);
        play_voice_ex(a, s, 0.60f, pitch, false);
    } else {
        Sound *s = pick(a, a->dig[m], a->dig_count[m]);
        play_voice_ex(a, s, 0.65f, pitch, false);
    }
}

void audio_play_step(Audio *a, uint8_t block) {
    if (!a) return;
    int m = material_for_block(block);
    float pitch = 0.95f + (float)(a->rng & 15) * 0.015f;
    Sound *s = pick(a, a->step[m], a->step_count[m]);
    play_voice_ex(a, s, 0.35f, pitch, false);
}

void audio_play_hurt(Audio *a) {
    if (!a) return;
    float pitch = 0.92f + (float)(a->rng & 15) * 0.012f;
    Sound *s = pick(a, a->hit, 3);
    play_voice_ex(a, s, 0.85f, pitch, false);
}

void audio_play_splash(Audio *a) {
    if (!a) return;
    float pitch = 0.90f + (float)(a->rng & 15) * 0.015f;
    Sound *s = pick(a, a->splash, 2);
    play_voice_ex(a, s, 0.75f, pitch, false);
}

void audio_play_pop(Audio *a) {
    if (!a) return;
    float pitch = 1.0f + (float)(a->rng & 15) * 0.02f;
    play_voice_ex(a, a->pop_snd, 0.50f, pitch, false);
}

void audio_play_explode(Audio *a) {
    if (!a) return;
    float pitch = 0.85f + (float)(a->rng & 15) * 0.015f;
    Sound *s = pick(a, a->explode, 4);
    play_voice_ex(a, s, 0.95f, pitch, false);
}

void audio_play_fuse(Audio *a) {
    if (!a) return;
    play_voice_ex(a, a->fuse_snd, 0.85f, 1.0f, false);
}

void audio_music_menu(Audio *a, bool on) {
    if (!a || !a->available)
        return;
    Voice *voices = (Voice *)a->voices;
    if (on) {
        if (voices[0].active && voices[0].sound == a->music_menu)
            return;
        voices[0].sound = a->music_menu;
        voices[0].pos = 0;
        voices[0].gain = 0.28f;
        voices[0].pitch = 1.0f;
        voices[0].loop = true;
        voices[0].active = a->music_menu != NULL;
    } else {
        voices[0].active = false;
    }
}

void audio_play_cave(Audio *a) {
    play_voice_ex(a, a->ambient_cave, 0.22f, 1.0f, false);
}

void audio_play_fizz(Audio *a) {
    if (!a) return;
    float pitch = 0.95f + (float)(a->rng & 15) * 0.01f;
    play_voice_ex(a, a->fizz_snd, 0.70f, pitch, false);
}

void audio_play_eat(Audio *a) {
    if (!a) return;
    float pitch = 0.95f + (float)(a->rng & 15) * 0.015f;
    Sound *s = pick(a, a->eat_snd, 3);
    play_voice_ex(a, s, 0.65f, pitch, false);
}

void audio_play_chest_open(Audio *a) {
    if (!a) return;
    play_voice_ex(a, a->chest_open_snd, 0.75f, 1.0f, false);
}

void audio_play_chest_close(Audio *a) {
    if (!a) return;
    play_voice_ex(a, a->chest_close_snd, 0.75f, 1.0f, false);
}

void audio_play_door_open(Audio *a) {
    if (!a) return;
    play_voice_ex(a, a->door_open_snd, 0.75f, 1.0f, false);
}

void audio_play_door_close(Audio *a) {
    if (!a) return;
    play_voice_ex(a, a->door_close_snd, 0.75f, 1.0f, false);
}

void audio_play_bow(Audio *a) {
    if (!a) return;
    float pitch = 0.95f + (float)(a->rng & 15) * 0.01f;
    play_voice_ex(a, a->bow_snd, 0.85f, pitch, false);
}

void audio_play_bow_hit(Audio *a) {
    if (!a) return;
    Sound *s = pick(a, a->bow_hit, 4);
    play_voice_ex(a, s, 0.85f, 1.0f, false);
}

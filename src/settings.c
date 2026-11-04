#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void settings_init_defaults(Settings *s) {
    if (!s) return;
    s->fov = 70;
    s->render_distance = 16;
    s->max_fps = 0;
    s->vsync = false;
    s->view_bobbing = true;
    s->clouds = true;
    s->fog = true;
    s->brightness = 0.5f;
    s->gui_scale = 0;
    s->fullscreen = false;

    s->master_volume = 1.0f;
    s->music_volume = 0.7f;
    s->sound_volume = 1.0f;
    s->blocks_volume = 1.0f;
    s->mute = false;

    s->mouse_sensitivity = 1.0f;
    s->invert_mouse = false;
}

bool settings_load(Settings *s, const char *path) {
    if (!s || !path) return false;
    settings_init_defaults(s);

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *key = line;
        while (*key == ' ' || *key == '\t') key++;
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        size_t len = strlen(val);
        while (len > 0 && (val[len - 1] == '\r' || val[len - 1] == '\n' || val[len - 1] == ' ')) {
            val[--len] = '\0';
        }

        if (strcmp(key, "fov") == 0) {
            s->fov = atoi(val);
            if (s->fov < 30) s->fov = 30;
            if (s->fov > 110) s->fov = 110;
        } else if (strcmp(key, "renderDistance") == 0) {
            s->render_distance = atoi(val);
            if (s->render_distance < 2) s->render_distance = 2;
            if (s->render_distance > 32) s->render_distance = 32;
        } else if (strcmp(key, "maxFps") == 0) {
            s->max_fps = atoi(val);
        } else if (strcmp(key, "enableVsync") == 0) {
            s->vsync = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "bobView") == 0) {
            s->view_bobbing = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "renderClouds") == 0) {
            s->clouds = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "fog") == 0) {
            s->fog = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "gamma") == 0) {
            s->brightness = (float)atof(val);
            if (s->brightness < 0.0f) s->brightness = 0.0f;
            if (s->brightness > 1.0f) s->brightness = 1.0f;
        } else if (strcmp(key, "guiScale") == 0) {
            s->gui_scale = atoi(val);
        } else if (strcmp(key, "fullscreen") == 0) {
            s->fullscreen = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "soundVolume") == 0) {
            s->master_volume = (float)atof(val);
            if (s->master_volume < 0.0f) s->master_volume = 0.0f;
            if (s->master_volume > 1.0f) s->master_volume = 1.0f;
        } else if (strcmp(key, "musicVolume") == 0) {
            s->music_volume = (float)atof(val);
            if (s->music_volume < 0.0f) s->music_volume = 0.0f;
            if (s->music_volume > 1.0f) s->music_volume = 1.0f;
        } else if (strcmp(key, "blocksVolume") == 0) {
            s->blocks_volume = (float)atof(val);
            if (s->blocks_volume < 0.0f) s->blocks_volume = 0.0f;
            if (s->blocks_volume > 1.0f) s->blocks_volume = 1.0f;
        } else if (strcmp(key, "mute") == 0) {
            s->mute = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "mouseSensitivity") == 0) {
            s->mouse_sensitivity = (float)atof(val);
            if (s->mouse_sensitivity < 0.2f) s->mouse_sensitivity = 0.2f;
            if (s->mouse_sensitivity > 3.0f) s->mouse_sensitivity = 3.0f;
        } else if (strcmp(key, "invertYMouse") == 0) {
            s->invert_mouse = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        }
    }
    fclose(f);
    return true;
}

bool settings_save(const Settings *s, const char *path) {
    if (!s || !path) return false;
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "# Minecraft C Edition options\n");
    fprintf(f, "version:1\n");
    fprintf(f, "fov:%d\n", s->fov);
    fprintf(f, "renderDistance:%d\n", s->render_distance);
    fprintf(f, "maxFps:%d\n", s->max_fps);
    fprintf(f, "enableVsync:%s\n", s->vsync ? "true" : "false");
    fprintf(f, "bobView:%s\n", s->view_bobbing ? "true" : "false");
    fprintf(f, "renderClouds:%s\n", s->clouds ? "true" : "false");
    fprintf(f, "fog:%s\n", s->fog ? "true" : "false");
    fprintf(f, "gamma:%.2f\n", s->brightness);
    fprintf(f, "guiScale:%d\n", s->gui_scale);
    fprintf(f, "fullscreen:%s\n", s->fullscreen ? "true" : "false");
    fprintf(f, "soundVolume:%.2f\n", s->master_volume);
    fprintf(f, "musicVolume:%.2f\n", s->music_volume);
    fprintf(f, "blocksVolume:%.2f\n", s->blocks_volume);
    fprintf(f, "mute:%s\n", s->mute ? "true" : "false");
    fprintf(f, "mouseSensitivity:%.2f\n", s->mouse_sensitivity);
    fprintf(f, "invertYMouse:%s\n", s->invert_mouse ? "true" : "false");

    fclose(f);
    return true;
}

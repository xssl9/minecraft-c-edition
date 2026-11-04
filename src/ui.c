#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define C_PANEL       ((Color3){0.20f, 0.20f, 0.22f})
#define C_PANEL_HI    ((Color3){0.38f, 0.38f, 0.42f})
#define C_PANEL_LO    ((Color3){0.10f, 0.10f, 0.12f})
#define C_SLOT        ((Color3){0.14f, 0.14f, 0.16f})
#define C_DARK        ((Color3){0.04f, 0.04f, 0.05f})
#define C_TEXT        ((Color3){0.92f, 0.92f, 0.94f})
#define C_GRAY        ((Color3){0.55f, 0.55f, 0.58f})
#define C_YELLOW      ((Color3){1.00f, 0.92f, 0.25f})
#define C_WHITE       ((Color3){1.00f, 1.00f, 1.00f})
#define C_RED         ((Color3){0.90f, 0.18f, 0.18f})
#define C_DEATH_RED   ((Color3){0.60f, 0.08f, 0.08f})
#define C_HEART_EMPTY ((Color3){0.18f, 0.06f, 0.06f})

static int g_palette[BLOCK_COUNT];
static int g_palette_count;

static void build_palette(void) {
    if (g_palette_count)
        return;
    for (int i = 1; i < BLOCK_COUNT; i++) {
        if (i == BLOCK_WATER || i == BLOCK_LAVA || i == BLOCK_AIR)
            continue;
        g_palette[g_palette_count++] = i;
    }
}

void ui_init(UI *ui, int width, int height) {
    memset(ui, 0, sizeof(*ui));
    ui->width = width;
    ui->height = height;
    ui->hover = -1;
    ui->active_slider = -1;
    ui->screen = UI_SCREEN_TITLE;
    snprintf(ui->world_name, sizeof(ui->world_name), "New World");
    ui->world_name_len = (int)strlen(ui->world_name);
    ui->world_seed[0] = '\0';
    ui->world_seed_len = 0;
    ui->focused_text_field = 1;
    ui->create_gamemode = 0;
    ui->create_difficulty = 2;
    ui->create_cheats = false;
    ui->show_more_options = false;
    ui->create_world_type = 0;
    ui->active_inv_tab = 0;
    ui->hovered_slot_type = SLOT_NONE;
    ui->hovered_slot_index = -1;
    build_palette();
}

void ui_set_settings(UI *ui, Settings *s) {
    ui->settings = s;
    ui_refresh_button_labels(ui);
}

static void add_btn(UI *ui, const char *label, UIAction action,
                    float x, float y, float w, float h, bool disabled) {
    if (ui->button_count >= UI_MAX_BUTTONS)
        return;
    UIButton *b = &ui->buttons[ui->button_count++];
    memset(b, 0, sizeof(*b));
    snprintf(b->label, sizeof(b->label), "%s", label ? label : "");
    b->action = action;
    b->type = WIDGET_BUTTON;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->disabled = disabled;
}

static void add_sld(UI *ui, SliderID sid, float x, float y, float w, float h) {
    if (ui->button_count >= UI_MAX_BUTTONS)
        return;
    UIButton *b = &ui->buttons[ui->button_count++];
    memset(b, 0, sizeof(*b));
    b->action = UI_ACT_NONE;
    b->type = WIDGET_SLIDER;
    b->slider_id = sid;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
}

void ui_refresh_button_labels(UI *ui) {
    if (!ui->settings) return;
    Settings *s = ui->settings;
    for (int i = 0; i < ui->button_count; i++) {
        UIButton *b = &ui->buttons[i];
        if (b->type == WIDGET_SLIDER) {
            switch (b->slider_id) {
            case SLIDER_FOV: {
                b->slider_val = (float)(s->fov - 30) / 80.0f;
                if (s->fov == 70)
                    snprintf(b->label, sizeof(b->label), "FOV: Normal");
                else if (s->fov >= 110)
                    snprintf(b->label, sizeof(b->label), "FOV: Quake Pro");
                else
                    snprintf(b->label, sizeof(b->label), "FOV: %d", s->fov);
                break;
            }
            case SLIDER_RENDER_DISTANCE: {
                b->slider_val = (float)(s->render_distance - 2) / 30.0f;
                snprintf(b->label, sizeof(b->label), "Render Distance: %d chunks", s->render_distance);
                break;
            }
            case SLIDER_MAX_FPS: {
                if (s->max_fps == 0) {
                    b->slider_val = 1.0f;
                    snprintf(b->label, sizeof(b->label), "Max Framerate: Unlimited");
                } else {
                    b->slider_val = (float)s->max_fps / 240.0f;
                    snprintf(b->label, sizeof(b->label), "Max Framerate: %d fps", s->max_fps);
                }
                break;
            }
            case SLIDER_BRIGHTNESS: {
                b->slider_val = s->brightness;
                if (s->brightness <= 0.05f)
                    snprintf(b->label, sizeof(b->label), "Brightness: Moody");
                else if (s->brightness >= 0.95f)
                    snprintf(b->label, sizeof(b->label), "Brightness: Bright");
                else
                    snprintf(b->label, sizeof(b->label), "Brightness: +%d%%", (int)(s->brightness * 100.0f));
                break;
            }
            case SLIDER_MASTER_VOL: {
                b->slider_val = s->master_volume;
                if (s->master_volume <= 0.01f)
                    snprintf(b->label, sizeof(b->label), "Master Volume: OFF");
                else
                    snprintf(b->label, sizeof(b->label), "Master Volume: %d%%", (int)(s->master_volume * 100.0f));
                break;
            }
            case SLIDER_MUSIC_VOL: {
                b->slider_val = s->music_volume;
                if (s->music_volume <= 0.01f)
                    snprintf(b->label, sizeof(b->label), "Music: OFF");
                else
                    snprintf(b->label, sizeof(b->label), "Music: %d%%", (int)(s->music_volume * 100.0f));
                break;
            }
            case SLIDER_BLOCKS_VOL: {
                b->slider_val = s->blocks_volume;
                if (s->blocks_volume <= 0.01f)
                    snprintf(b->label, sizeof(b->label), "Blocks: OFF");
                else
                    snprintf(b->label, sizeof(b->label), "Blocks: %d%%", (int)(s->blocks_volume * 100.0f));
                break;
            }
            case SLIDER_SENSITIVITY: {
                b->slider_val = (s->mouse_sensitivity - 0.2f) / 2.8f;
                if (s->mouse_sensitivity <= 0.25f)
                    snprintf(b->label, sizeof(b->label), "Sensitivity: *YAWN*");
                else if (s->mouse_sensitivity >= 2.95f)
                    snprintf(b->label, sizeof(b->label), "Sensitivity: HYPERSPEED!!!");
                else
                    snprintf(b->label, sizeof(b->label), "Sensitivity: %d%%", (int)(s->mouse_sensitivity * 100.0f));
                break;
            }
            default:
                break;
            }
        } else {
            switch (b->action) {
            case UI_ACT_TOGGLE_VSYNC:
                snprintf(b->label, sizeof(b->label), "VSync: %s", s->vsync ? "ON" : "OFF");
                break;
            case UI_ACT_TOGGLE_BOBBING:
                snprintf(b->label, sizeof(b->label), "View Bobbing: %s", s->view_bobbing ? "ON" : "OFF");
                break;
            case UI_ACT_TOGGLE_CLOUDS:
                snprintf(b->label, sizeof(b->label), "Clouds: %s", s->clouds ? "ON" : "OFF");
                break;
            case UI_ACT_TOGGLE_FOG:
                snprintf(b->label, sizeof(b->label), "Fog: %s", s->fog ? "ON" : "OFF");
                break;
            case UI_ACT_TOGGLE_FULLSCREEN:
                snprintf(b->label, sizeof(b->label), "Fullscreen: %s", s->fullscreen ? "ON" : "OFF");
                break;
            case UI_ACT_TOGGLE_MUTE:
                snprintf(b->label, sizeof(b->label), "Mute: %s", s->mute ? "ON" : "OFF");
                break;
            case UI_ACT_TOGGLE_INVERT_MOUSE:
                snprintf(b->label, sizeof(b->label), "Invert Mouse: %s", s->invert_mouse ? "ON" : "OFF");
                break;
            case UI_ACT_CYCLE_GUI_SCALE:
                if (s->gui_scale == 0)
                    snprintf(b->label, sizeof(b->label), "GUI Scale: Auto");
                else
                    snprintf(b->label, sizeof(b->label), "GUI Scale: %dx", s->gui_scale);
                break;
            case UI_ACT_TOGGLE_GAMEMODE:
                if (ui->create_gamemode == 1) snprintf(b->label, sizeof(b->label), "Game Mode: Creative");
                else if (ui->create_gamemode == 2) snprintf(b->label, sizeof(b->label), "Game Mode: Hardcore");
                else snprintf(b->label, sizeof(b->label), "Game Mode: Survival");
                break;
            case UI_ACT_TOGGLE_DIFFICULTY: {
                static const char *diff_names[] = {"Peaceful", "Easy", "Normal", "Hard"};
                snprintf(b->label, sizeof(b->label), "Difficulty: %s", diff_names[ui->create_difficulty & 3]);
                break;
            }
            case UI_ACT_TOGGLE_CHEATS:
                snprintf(b->label, sizeof(b->label), "Allow Cheats: %s", ui->create_cheats ? "ON" : "OFF");
                break;
            case UI_ACT_TOGGLE_WORLD_TYPE:
                snprintf(b->label, sizeof(b->label), "World Type: %s", ui->create_world_type == 1 ? "Superflat" : "Default");
                break;
            default:
                break;
            }
        }
    }
}

static void apply_slider_drag(UI *ui, int slider_idx, float mx) {
    if (slider_idx < 0 || slider_idx >= ui->button_count || !ui->settings) return;
    UIButton *b = &ui->buttons[slider_idx];
    if (b->type != WIDGET_SLIDER) return;

    float t = (mx - (b->x + 4.0f)) / (b->w - 8.0f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    b->slider_val = t;

    Settings *s = ui->settings;
    switch (b->slider_id) {
    case SLIDER_FOV:
        s->fov = 30 + (int)(t * 80.0f);
        break;
    case SLIDER_RENDER_DISTANCE:
        s->render_distance = 2 + (int)(t * 30.0f);
        break;
    case SLIDER_MAX_FPS: {
        int fps_steps[] = {30, 60, 90, 120, 144, 240, 0};
        int step = (int)(t * 6.99f);
        if (step > 6) step = 6;
        s->max_fps = fps_steps[step];
        break;
    }
    case SLIDER_BRIGHTNESS:
        s->brightness = t;
        break;
    case SLIDER_MASTER_VOL:
        s->master_volume = t;
        break;
    case SLIDER_MUSIC_VOL:
        s->music_volume = t;
        break;
    case SLIDER_BLOCKS_VOL:
        s->blocks_volume = t;
        break;
    case SLIDER_SENSITIVITY:
        s->mouse_sensitivity = 0.2f + t * 2.8f;
        break;
    default:
        break;
    }
    ui_refresh_button_labels(ui);
}

static void layout_title(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    float y = 136;
    add_btn(ui, ui->has_save ? "Continue World" : "Create World", UI_ACT_CONTINUE, cx - 100, y, 200, 20, false);
    add_btn(ui, "New World", UI_ACT_NEW_WORLD, cx - 100, y + 24, 200, 20, false);
    add_btn(ui, "Options...", UI_ACT_OPEN_OPTIONS, cx - 100, y + 48, 98, 20, false);
    add_btn(ui, "Quit Game", UI_ACT_QUIT, cx + 2, y + 48, 98, 20, false);
    ui_refresh_button_labels(ui);
}

static void layout_pause(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    float py = 34;
    add_btn(ui, "Back to Game", UI_ACT_RESUME, cx - 100, py + 42, 200, 20, false);
    add_btn(ui, "Advancements", UI_ACT_OPEN_ADVANCEMENTS, cx - 100, py + 66, 98, 20, false);
    add_btn(ui, "Options...", UI_ACT_OPEN_OPTIONS, cx + 2, py + 66, 98, 20, false);
    add_btn(ui, "Save and Quit to Title", UI_ACT_SAVE_QUIT, cx - 100, py + 90, 200, 20, false);
    ui_refresh_button_labels(ui);
}

static void layout_options(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    add_sld(ui, SLIDER_FOV, cx - 155, 46, 150, 20);
    add_btn(ui, "Super Secret Settings", UI_ACT_NONE, cx + 5, 46, 150, 20, true);
    add_btn(ui, "Video Settings...", UI_ACT_OPEN_VIDEO, cx - 155, 74, 150, 20, false);
    add_btn(ui, "Music & Sounds...", UI_ACT_OPEN_AUDIO, cx + 5, 74, 150, 20, false);
    add_btn(ui, "Controls...", UI_ACT_OPEN_CONTROLS, cx - 155, 102, 150, 20, false);
    add_btn(ui, "Advancements...", UI_ACT_OPEN_ADVANCEMENTS, cx + 5, 102, 150, 20, false);
    add_btn(ui, "Done", UI_ACT_DONE, cx - 100, (float)ui->height - 30, 200, 20, false);
    ui_refresh_button_labels(ui);
}

static void layout_video(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    add_sld(ui, SLIDER_RENDER_DISTANCE, cx - 155, 42, 150, 20);
    add_sld(ui, SLIDER_MAX_FPS, cx + 5, 42, 150, 20);
    add_btn(ui, "Clouds: ON", UI_ACT_TOGGLE_CLOUDS, cx - 155, 66, 150, 20, false);
    add_btn(ui, "Fog: ON", UI_ACT_TOGGLE_FOG, cx + 5, 66, 150, 20, false);
    add_btn(ui, "View Bobbing: ON", UI_ACT_TOGGLE_BOBBING, cx - 155, 90, 150, 20, false);
    add_btn(ui, "Fullscreen: OFF", UI_ACT_TOGGLE_FULLSCREEN, cx + 5, 90, 150, 20, false);
    add_btn(ui, "GUI Scale: Auto", UI_ACT_CYCLE_GUI_SCALE, cx - 155, 114, 150, 20, false);
    add_sld(ui, SLIDER_BRIGHTNESS, cx + 5, 114, 150, 20);
    add_btn(ui, "VSync: OFF", UI_ACT_TOGGLE_VSYNC, cx - 155, 138, 150, 20, false);
    add_btn(ui, "Done", UI_ACT_DONE, cx - 100, (float)ui->height - 30, 200, 20, false);
    ui_refresh_button_labels(ui);
}

static void layout_audio(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    add_sld(ui, SLIDER_MASTER_VOL, cx - 100, 44, 200, 20);
    add_sld(ui, SLIDER_MUSIC_VOL, cx - 155, 74, 150, 20);
    add_sld(ui, SLIDER_BLOCKS_VOL, cx + 5, 74, 150, 20);
    add_btn(ui, "Mute: OFF", UI_ACT_TOGGLE_MUTE, cx - 100, 104, 200, 20, false);
    add_btn(ui, "Done", UI_ACT_DONE, cx - 100, (float)ui->height - 30, 200, 20, false);
    ui_refresh_button_labels(ui);
}

static void layout_controls(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    add_sld(ui, SLIDER_SENSITIVITY, cx - 155, 42, 150, 20);
    add_btn(ui, "Invert Mouse: OFF", UI_ACT_TOGGLE_INVERT_MOUSE, cx + 5, 42, 150, 20, false);
    add_btn(ui, "Done", UI_ACT_DONE, cx - 100, (float)ui->height - 30, 200, 20, false);
    ui_refresh_button_labels(ui);
}

static void layout_advancements(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    add_btn(ui, "Done", UI_ACT_DONE, cx - 100, (float)ui->height - 28, 200, 20, false);
    ui_refresh_button_labels(ui);
}

static void layout_death(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;
    add_btn(ui, "Respawn", UI_ACT_RESPAWN, cx - 100, 116, 200, 20, false);
    add_btn(ui, "Title Screen", UI_ACT_DEATH_TITLE, cx - 100, 145, 200, 20, false);
}

static void layout_inventory(UI *ui) {
    ui->button_count = 0;
}

static void layout_create_world(UI *ui) {
    ui->button_count = 0;
    float cx = ui->width * 0.5f;

    if (!ui->show_more_options) {
        const char *gm_label = "Game Mode: Survival";
        if (ui->create_gamemode == 1) gm_label = "Game Mode: Creative";
        else if (ui->create_gamemode == 2) gm_label = "Game Mode: Hardcore";
        add_btn(ui, gm_label, UI_ACT_TOGGLE_GAMEMODE, cx - 100, 68, 200, 20, false);

        static const char *diff_names[] = {"Peaceful", "Easy", "Normal", "Hard"};
        char diff_buf[64];
        snprintf(diff_buf, sizeof(diff_buf), "Difficulty: %s", diff_names[ui->create_difficulty & 3]);
        add_btn(ui, diff_buf, UI_ACT_TOGGLE_DIFFICULTY, cx - 100, 102, 200, 20, false);

        char cheats_buf[64];
        snprintf(cheats_buf, sizeof(cheats_buf), "Allow Cheats: %s", ui->create_cheats ? "ON" : "OFF");
        add_btn(ui, cheats_buf, UI_ACT_TOGGLE_CHEATS, cx - 100, 126, 200, 20, false);

        add_btn(ui, "More World Options...", UI_ACT_MORE_WORLD_OPTIONS, cx - 100, 150, 200, 20, false);
    } else {
        const char *wt_label = ui->create_world_type == 1 ? "World Type: Superflat" : "World Type: Default";
        add_btn(ui, wt_label, UI_ACT_TOGGLE_WORLD_TYPE, cx - 100, 84, 200, 20, false);
        add_btn(ui, "Done", UI_ACT_MORE_WORLD_OPTIONS, cx - 100, 120, 200, 20, false);
    }

    add_btn(ui, "Create New World", UI_ACT_CREATE_WORLD_START, cx - 155, (float)ui->height - 28, 150, 20, false);
    add_btn(ui, "Cancel", UI_ACT_CREATE_WORLD_CANCEL, cx + 5, (float)ui->height - 28, 150, 20, false);
}

void ui_set_screen(UI *ui, UIScreen screen, bool has_save) {
    ui->screen = screen;
    ui->has_save = has_save;
    ui->hover = -1;
    ui->active_slider = -1;

    switch (screen) {
    case UI_SCREEN_TITLE:
        layout_title(ui);
        break;
    case UI_SCREEN_CREATE_WORLD:
        layout_create_world(ui);
        break;
    case UI_SCREEN_PAUSE:
        layout_pause(ui);
        break;
    case UI_SCREEN_OPTIONS:
        layout_options(ui);
        break;
    case UI_SCREEN_VIDEO:
        layout_video(ui);
        break;
    case UI_SCREEN_AUDIO:
        layout_audio(ui);
        break;
    case UI_SCREEN_CONTROLS:
        layout_controls(ui);
        break;
    case UI_SCREEN_ADVANCEMENTS:
        layout_advancements(ui);
        break;
    case UI_SCREEN_INVENTORY:
    case UI_SCREEN_CRAFTING:
    case UI_SCREEN_FURNACE:
    case UI_SCREEN_CHEST:
        layout_inventory(ui);
        break;
    case UI_SCREEN_DEATH:
        layout_death(ui);
        break;
    default:
        ui->button_count = 0;
        break;
    }
}

void ui_mouse_move(UI *ui, float x, float y) {
    ui->mouse_x = x;
    ui->mouse_y = y;
    if (ui->active_slider >= 0) {
        apply_slider_drag(ui, ui->active_slider, x);
        return;
    }
    ui->hover = -1;
    for (int i = 0; i < ui->button_count; i++) {
        UIButton *b = &ui->buttons[i];
        if (x >= b->x && x <= b->x + b->w && y >= b->y && y <= b->y + b->h) {
            ui->hover = i;
            break;
        }
    }
}

bool ui_mouse_down(UI *ui, float x, float y) {
    for (int i = 0; i < ui->button_count; i++) {
        UIButton *b = &ui->buttons[i];
        if (x >= b->x && x <= b->x + b->w && y >= b->y && y <= b->y + b->h) {
            if (b->type == WIDGET_SLIDER) {
                ui->active_slider = i;
                apply_slider_drag(ui, i, x);
                return true;
            }
        }
    }
    return false;
}

void ui_mouse_up(UI *ui) {
    ui->active_slider = -1;
}

void ui_scroll_inventory(UI *ui, int direction) {
    if (ui->screen != UI_SCREEN_INVENTORY)
        return;
    int pages = (g_palette_count + 44) / 45;
    if (pages < 1) pages = 1;
    ui->inv_page += direction;
    while (ui->inv_page < 0) ui->inv_page += pages;
    while (ui->inv_page >= pages) ui->inv_page -= pages;
}

void ui_text_input_char(UI *ui, char c) {
    if (ui->screen != UI_SCREEN_CREATE_WORLD)
        return;
    if ((unsigned char)c < 32 || (unsigned char)c > 126)
        return;
    if (ui->focused_text_field == 1) {
        if (ui->world_name_len + 1 < (int)sizeof(ui->world_name)) {
            ui->world_name[ui->world_name_len++] = c;
            ui->world_name[ui->world_name_len] = '\0';
        }
    } else if (ui->focused_text_field == 2) {
        if (ui->world_seed_len + 1 < (int)sizeof(ui->world_seed)) {
            ui->world_seed[ui->world_seed_len++] = c;
            ui->world_seed[ui->world_seed_len] = '\0';
        }
    }
}

void ui_text_input_backspace(UI *ui) {
    if (ui->screen != UI_SCREEN_CREATE_WORLD)
        return;
    if (ui->focused_text_field == 1) {
        if (ui->world_name_len > 0)
            ui->world_name[--ui->world_name_len] = '\0';
    } else if (ui->focused_text_field == 2) {
        if (ui->world_seed_len > 0)
            ui->world_seed[--ui->world_seed_len] = '\0';
    }
}

void ui_slot_click_event(UI *ui, Player *p, bool right_click, bool shift_click) {
    if (!p) return;
    if (ui->hovered_slot_type == SLOT_NONE) return;

    if (ui->screen == UI_SCREEN_INVENTORY) {
        if (!p->creative || ui->hovered_slot_type != SLOT_CREATIVE_PALETTE) {
            player_slot_click(p, ui->hovered_slot_type, ui->hovered_slot_index, right_click, shift_click);
        } else if (ui->hovered_slot_type == SLOT_CREATIVE_PALETTE) {
            if (ui->hovered_slot_index >= 0 && ui->hovered_slot_index < g_palette_count) {
                p->hotbar[p->selected] = (uint8_t)g_palette[ui->hovered_slot_index];
                p->hotbar_counts[p->selected] = 64;
            }
        }
    } else if (ui->screen == UI_SCREEN_CRAFTING) {
        player_slot_click_crafting_3x3(p, ui->hovered_slot_type, ui->hovered_slot_index, right_click, shift_click);
    } else if (ui->screen == UI_SCREEN_FURNACE) {
        player_slot_click_furnace(p, ui->hovered_slot_type, ui->hovered_slot_index, right_click, shift_click);
    } else if (ui->screen == UI_SCREEN_CHEST) {
        player_slot_click_chest(p, ui->hovered_slot_type, ui->hovered_slot_index, right_click, shift_click);
    }
}

UIAction ui_click(UI *ui, float x, float y, Player *p) {
    ui->clicked_block = 0;

    if (ui->screen == UI_SCREEN_CREATE_WORLD) {
        float cx = ui->width * 0.5f;
        if (!ui->show_more_options) {
            if (x >= cx - 100 && x <= cx + 100 && y >= 36 && y <= 56) {
                ui->focused_text_field = 1;
                return UI_ACT_NONE;
            }
        } else {
            if (x >= cx - 100 && x <= cx + 100 && y >= 48 && y <= 68) {
                ui->focused_text_field = 2;
                return UI_ACT_NONE;
            }
        }
    }

    for (int i = 0; i < ui->button_count; i++) {
        UIButton *b = &ui->buttons[i];
        if (x >= b->x && x <= b->x + b->w && y >= b->y && y <= b->y + b->h) {
            if (b->disabled)
                return UI_ACT_NONE;
            if (b->type == WIDGET_SLIDER) {
                apply_slider_drag(ui, i, x);
                return UI_ACT_NONE;
            }
            if (b->action == UI_ACT_MORE_WORLD_OPTIONS) {
                ui->show_more_options = !ui->show_more_options;
                ui->focused_text_field = ui->show_more_options ? 2 : 1;
                layout_create_world(ui);
                return UI_ACT_NONE;
            }
            if (b->action == UI_ACT_TOGGLE_GAMEMODE) {
                ui->create_gamemode = (ui->create_gamemode + 1) % 3;
                layout_create_world(ui);
                return UI_ACT_NONE;
            }
            if (b->action == UI_ACT_TOGGLE_DIFFICULTY) {
                ui->create_difficulty = (ui->create_difficulty + 1) % 4;
                layout_create_world(ui);
                return UI_ACT_NONE;
            }
            if (b->action == UI_ACT_TOGGLE_CHEATS) {
                ui->create_cheats = !ui->create_cheats;
                layout_create_world(ui);
                return UI_ACT_NONE;
            }
            if (b->action == UI_ACT_TOGGLE_WORLD_TYPE) {
                ui->create_world_type = (ui->create_world_type + 1) % 2;
                layout_create_world(ui);
                return UI_ACT_NONE;
            }
            return b->action;
        }
    }

    if (ui->screen == UI_SCREEN_INVENTORY && p) {
        if (ui->hovered_slot_type != SLOT_NONE) {
            if (!p->creative || ui->hovered_slot_type != SLOT_CREATIVE_PALETTE) {
                player_slot_click(p, ui->hovered_slot_type, ui->hovered_slot_index, false, false);
                return UI_ACT_NONE;
            } else if (ui->hovered_slot_type == SLOT_CREATIVE_PALETTE) {
                if (ui->hovered_slot_index >= 0 && ui->hovered_slot_index < g_palette_count) {
                    p->hotbar[p->selected] = (uint8_t)g_palette[ui->hovered_slot_index];
                    p->hotbar_counts[p->selected] = 64;
                    ui->clicked_block = g_palette[ui->hovered_slot_index];
                    return UI_ACT_PICK_BLOCK;
                }
            }
        }
    }

    return UI_ACT_NONE;
}

static void draw_button(Renderer *r, UI *ui, int index) {
    UIButton *b = &ui->buttons[index];
    bool hover = (ui->hover == index || ui->active_slider == index);
    if (b->type == WIDGET_SLIDER) {
        renderer_slider(r, b->x, b->y, b->w, b->h, b->slider_val, b->label, hover);
    } else {
        renderer_button(r, b->x, b->y, b->w, b->h, b->label, hover, b->disabled);
    }
}

static void draw_title(UI *ui, Renderer *r) {
    renderer_menu_background(r);
    renderer_logo(r, (ui->width - 330) * 0.5f, 22, 330, 82);
    renderer_text_centered(r, ui->width * 0.5f, 108, 1.5f, "C  E D I T I O N",
                           C_YELLOW, true);
    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
    renderer_text_centered(r, ui->width * 0.5f, ui->height - 24, 0.8f,
                           "Assets imported from your local Minecraft 1.21.1 installation",
                           C_GRAY, false);
    renderer_text_centered(r, ui->width * 0.5f, ui->height - 13, 0.75f,
                           "For personal use only. Do not redistribute Mojang assets.",
                           (Color3){0.78f, 0.66f, 0.42f}, false);
    renderer_text(r, ui->width - 76, 6, 0.75f, "v0.1", C_GRAY, false);
}

static void draw_pause(UI *ui, Renderer *r, Player *p) {
    float cx = ui->width * 0.5f;
    float px = cx - 150, py = 34, pw = 300, ph = 158;
    renderer_rect(r, px - 2, py - 2, pw + 4, ph + 4, C_DARK);
    renderer_rect(r, px, py, pw, ph, C_PANEL);
    renderer_rect_outline(r, px, py, pw, ph, 2, C_PANEL_HI);
    renderer_text_centered(r, cx, py + 10, 2.0f, "Game Paused", C_TEXT, true);
    char line[160];
    snprintf(line, sizeof(line), "XYZ %.1f  %.1f  %.1f", p->pos.x, p->pos.y, p->pos.z);
    renderer_text_centered(r, cx, py + 30, 0.75f, line, C_GRAY, false);
    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
}

static void draw_options(UI *ui, Renderer *r) {
    renderer_dirt_background(r);
    float cx = ui->width * 0.5f;
    renderer_text_centered(r, cx, 16, 1.4f, "Options", C_WHITE, true);
    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
}

static void draw_video(UI *ui, Renderer *r) {
    renderer_dirt_background(r);
    float cx = ui->width * 0.5f;
    renderer_text_centered(r, cx, 16, 1.4f, "Video Settings", C_WHITE, true);
    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
}

static void draw_audio(UI *ui, Renderer *r) {
    renderer_dirt_background(r);
    float cx = ui->width * 0.5f;
    renderer_text_centered(r, cx, 16, 1.4f, "Music & Sounds", C_WHITE, true);
    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
}

static void draw_controls(UI *ui, Renderer *r) {
    renderer_dirt_background(r);
    float cx = ui->width * 0.5f;
    renderer_text_centered(r, cx, 16, 1.4f, "Controls", C_WHITE, true);
    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);

    /* Keybinds summary card */
    float kx = cx - 155, ky = 76, kw = 310, kh = 120;
    renderer_rect(r, kx, ky, kw, kh, (Color3){0.08f, 0.08f, 0.09f});
    renderer_rect_outline(r, kx, ky, kw, kh, 1, (Color3){0.25f, 0.25f, 0.28f});

    static const char *binds[][2] = {
        {"Movement", "W / A / S / D"},
        {"Jump / Swim", "Space"},
        {"Sneak / Descend", "Left Shift"},
        {"Sprint", "Left Ctrl"},
        {"Toggle Flight", "F"},
        {"Creative / Survival", "F4"},
        {"Inventory", "E"},
        {"Chat / Command", "T / /"},
        {"Screenshot / Debug", "F2 / F3"},
        {"Fullscreen / Pause", "F11 / Esc"}
    };
    int count = 10;
    for (int i = 0; i < count; i++) {
        float ly = ky + 6 + (float)i * 11;
        renderer_text(r, kx + 8, ly, 0.75f, binds[i][0], C_GRAY, false);
        renderer_text(r, kx + kw - 8 - (float)renderer_text_width(r, 0.75f, binds[i][1]),
                      ly, 0.75f, binds[i][1], C_WHITE, false);
    }
}

static void draw_advancements(UI *ui, Renderer *r) {
    renderer_dirt_background(r);
    float cx = ui->width * 0.5f;
    renderer_text_centered(r, cx, 16, 1.4f, "Advancements", C_WHITE, true);

    float ww = 252, wh = 140;
    float wx = cx - ww * 0.5f, wy = 40;

    /* Window background */
    renderer_rect(r, wx, wy, ww, wh, (Color3){0.05f, 0.05f, 0.06f});
    renderer_rect_outline(r, wx, wy, ww, wh, 2, (Color3){0.35f, 0.35f, 0.40f});

    /* Interior stone area */
    float ix = wx + 8, iy = wy + 16, iw = ww - 16, ih = wh - 24;
    renderer_rect(r, ix, iy, iw, ih, (Color3){0.15f, 0.15f, 0.17f});

    /* Connecting branch lines between advancement nodes */
    glColor3f(0.45f, 0.45f, 0.48f);
    glBegin(GL_LINES);
    glVertex2f(ix + 24, iy + 52); glVertex2f(ix + 70, iy + 28);
    glVertex2f(ix + 24, iy + 52); glVertex2f(ix + 70, iy + 76);
    glVertex2f(ix + 70, iy + 28); glVertex2f(ix + 120, iy + 28);
    glVertex2f(ix + 70, iy + 76); glVertex2f(ix + 120, iy + 76);
    glVertex2f(ix + 120, iy + 28); glVertex2f(ix + 170, iy + 28);
    glVertex2f(ix + 120, iy + 76); glVertex2f(ix + 170, iy + 76);
    glEnd();

    typedef struct {
        uint8_t icon;
        const char *title;
        const char *desc;
        float x, y;
    } AdvNode;

    AdvNode nodes[] = {
        {BLOCK_GRASS_BLOCK, "Minecraft", "The heart and story of the game", ix + 24, iy + 52},
        {BLOCK_COBBLESTONE, "Stone Age", "Mine stone with your new pickaxe", ix + 70, iy + 28},
        {BLOCK_CRAFTING_TABLE, "Benchmarking", "300+ FPS voxel engine running", ix + 70, iy + 76},
        {BLOCK_IRON_BLOCK, "Acquire Hardware", "Smelt an iron ingot", ix + 120, iy + 28},
        {BLOCK_BEDROCK, "Far Lands", "Render 32 chunks into the distance", ix + 120, iy + 76},
        {BLOCK_FURNACE, "Hot Topic", "Construct a furnace out of cobblestone", ix + 170, iy + 28},
        {BLOCK_TNT, "Kaboom!", "Ignite TNT and trigger a chain reaction", ix + 170, iy + 76}
    };
    int node_count = 7;
    int hovered_node = -1;

    for (int i = 0; i < node_count; i++) {
        float nx = nodes[i].x - 10, ny = nodes[i].y - 10;
        bool hover = (ui->mouse_x >= nx && ui->mouse_x <= nx + 20 &&
                      ui->mouse_y >= ny && ui->mouse_y <= ny + 20);
        if (hover) hovered_node = i;

        renderer_rect(r, nx, ny, 20, 20, hover ? (Color3){0.55f, 0.55f, 0.2f} : (Color3){0.25f, 0.25f, 0.28f});
        renderer_rect_outline(r, nx, ny, 20, 20, 1.5f, hover ? C_YELLOW : C_WHITE);
        renderer_block_icon(r, nodes[i].icon, nx + 2, ny + 2, 16);
    }

    /* Tooltip */
    if (hovered_node >= 0) {
        AdvNode *n = &nodes[hovered_node];
        float tx = ui->mouse_x + 8, ty = ui->mouse_y - 20;
        float tw = (float)renderer_text_width(r, 0.85f, n->desc) + 12;
        renderer_rect(r, tx, ty, tw, 22, (Color3){0.04f, 0.04f, 0.05f});
        renderer_rect_outline(r, tx, ty, tw, 22, 1, C_YELLOW);
        renderer_text(r, tx + 6, ty + 3, 0.85f, n->title, C_YELLOW, true);
        renderer_text(r, tx + 6, ty + 12, 0.75f, n->desc, C_WHITE, false);
    }

    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
}

static void draw_death(UI *ui, Renderer *r) {
    float cx = ui->width * 0.5f;
    renderer_rect(r, 0, 0, (float)ui->width, 5, C_DEATH_RED);
    renderer_rect(r, 0, (float)ui->height - 5, (float)ui->width, 5, C_DEATH_RED);
    renderer_rect(r, 0, 0, 5, (float)ui->height, C_DEATH_RED);
    renderer_rect(r, (float)ui->width - 5, 0, 5, (float)ui->height, C_DEATH_RED);

    float px = cx - 160, py = 34, pw = 320, ph = 168;
    renderer_rect(r, px - 2, py - 2, pw + 4, ph + 4, C_DARK);
    renderer_rect(r, px, py, pw, ph, C_PANEL);
    renderer_rect_outline(r, px, py, pw, ph, 2, C_DEATH_RED);
    renderer_text_centered(r, cx, py + 14, 2.8f, "YOU DIED!", C_RED, true);
    renderer_text_centered(r, cx, py + 46, 0.9f, "Your health ran out.", C_GRAY, false);
    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
    renderer_text_centered(r, cx, py + ph - 15, 0.75f,
                           "Respawn at the world spawn point", C_GRAY, false);
}

static void draw_create_world(UI *ui, Renderer *r) {
    renderer_dirt_background(r);
    float cx = ui->width * 0.5f;
    renderer_text_centered(r, cx, 14, 1.4f, "Create New World", C_WHITE, true);

    bool blink = ((int)(ui->anim * 4.0f)) & 1;

    if (!ui->show_more_options) {
        renderer_text(r, cx - 100, 24, 0.85f, "World Name", C_GRAY, true);

        float bx = cx - 100, by = 36, bw = 200, bh = 20;
        renderer_rect(r, bx - 1, by - 1, bw + 2, bh + 2,
                      ui->focused_text_field == 1 ? C_WHITE : (Color3){0.4f, 0.4f, 0.4f});
        renderer_rect(r, bx, by, bw, bh, (Color3){0.04f, 0.04f, 0.05f});

        char display_name[80];
        snprintf(display_name, sizeof(display_name), "%s%s",
                 ui->world_name, (ui->focused_text_field == 1 && blink) ? "_" : "");
        renderer_text(r, bx + 4, by + 5, 0.95f, display_name, C_WHITE, false);

        const char *desc = "Search for resources, crafting, gain levels, health and hunger";
        if (ui->create_gamemode == 1)
            desc = "Unlimited resources, free flying and destroy blocks instantly";
        else if (ui->create_gamemode == 2)
            desc = "Same as survival mode, locked at hardest difficulty, and one life only";
        renderer_text_centered(r, cx, 91, 0.70f, desc, C_GRAY, false);
    } else {
        renderer_text(r, cx - 100, 36, 0.85f, "Seed for the world generator", C_GRAY, true);

        float bx = cx - 100, by = 48, bw = 200, bh = 20;
        renderer_rect(r, bx - 1, by - 1, bw + 2, bh + 2,
                      ui->focused_text_field == 2 ? C_WHITE : (Color3){0.4f, 0.4f, 0.4f});
        renderer_rect(r, bx, by, bw, bh, (Color3){0.04f, 0.04f, 0.05f});

        char display_seed[80];
        snprintf(display_seed, sizeof(display_seed), "%s%s",
                 ui->world_seed, (ui->focused_text_field == 2 && blink) ? "_" : "");
        renderer_text(r, bx + 4, by + 5, 0.95f, display_seed, C_WHITE, false);

        renderer_text_centered(r, cx, 71, 0.70f, "Leave blank for a random seed", C_GRAY, false);
    }

    for (int i = 0; i < ui->button_count; i++)
        draw_button(r, ui, i);
}

static void draw_crosshair(Renderer *r, UI *ui) {
    float cx = ui->width * 0.5f, cy = ui->height * 0.5f;
    if (r->gui_crosshair_tex) {
        renderer_sprite(r, r->gui_crosshair_tex, cx - 7.5f, cy - 7.5f, 15, 15);
    } else {
        Color3 sh = {0.05f, 0.05f, 0.05f};
        renderer_rect(r, cx - 6, cy - 1, 13, 3, sh);
        renderer_rect(r, cx - 1, cy - 6, 3, 13, sh);
        renderer_rect(r, cx - 5, cy, 11, 1, C_WHITE);
        renderer_rect(r, cx, cy - 5, 1, 11, C_WHITE);
    }
}

static void draw_hud(Renderer *r, UI *ui, Player *p) {
    float cx = ui->width * 0.5f;
    float y_hotbar = (float)ui->height - 22.0f;
    float x_hotbar = cx - 91.0f;

    /* 1. Hotbar background */
    if (r->gui_hotbar_tex) {
        renderer_sprite(r, r->gui_hotbar_tex, x_hotbar, y_hotbar, 182, 22);
    } else {
        renderer_rect(r, x_hotbar - 2, y_hotbar - 2, 186, 26, C_DARK);
    }

    /* 2. Hotbar slots & items */
    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        float sx = x_hotbar + 3.0f + (float)i * 20.0f;
        float sy = y_hotbar + 3.0f;
        uint8_t item = p->hotbar[i];
        if (item != BLOCK_AIR) {
            renderer_block_icon(r, item, sx, sy, 16);
            int count = p->hotbar_counts[i];
            if (count > 1) {
                char cnt_str[8];
                snprintf(cnt_str, sizeof(cnt_str), "%d", count);
                renderer_text(r, sx + 9, sy + 8, 0.75f, cnt_str, C_WHITE, true);
            }
        }
    }

    /* 3. Slot selection cursor */
    float sel_x = x_hotbar + (float)p->selected * 20.0f - 1.0f;
    float sel_y = y_hotbar - 1.0f;
    if (r->gui_hotbar_sel_tex) {
        renderer_sprite(r, r->gui_hotbar_sel_tex, sel_x, sel_y, 24, 23);
    } else {
        renderer_rect_outline(r, sel_x, sel_y, 22, 22, 2, C_WHITE);
    }

    /* 4. Selected item name tooltip */
    if (p->hotbar[p->selected] != BLOCK_AIR) {
        const char *name = g_block_defs[p->hotbar[p->selected]].name;
        renderer_text_centered(r, cx, y_hotbar - 19.0f, 0.85f, name, C_WHITE, true);
    }

    /* Survival Mode HUD elements */
    if (!p->creative) {
        float y_exp = y_hotbar - 7.0f;

        /* Experience Bar */
        if (r->gui_exp_bg_tex) {
            renderer_sprite(r, r->gui_exp_bg_tex, x_hotbar, y_exp, 182, 5);
        } else {
            renderer_rect(r, x_hotbar, y_exp, 182, 5, (Color3){0.1f, 0.1f, 0.1f});
        }

        float exp_frac = p->exp_progress;
        if (exp_frac > 1.0f) exp_frac = 1.0f;
        if (exp_frac > 0.001f) {
            float exp_w = 182.0f * exp_frac;
            if (r->gui_exp_prog_tex) {
                renderer_sprite_uv(r, r->gui_exp_prog_tex, x_hotbar, y_exp, exp_w, 5,
                                   0.0f, 0.0f, exp_frac, 1.0f);
            } else {
                renderer_rect(r, x_hotbar, y_exp, exp_w, 5, (Color3){0.50f, 1.0f, 0.12f});
            }
        }

        /* Experience level */
        if (p->level > 0) {
            char lvl_str[16];
            snprintf(lvl_str, sizeof(lvl_str), "%d", p->level);
            renderer_text_centered(r, cx, y_exp - 6.0f, 0.85f, lvl_str,
                                   (Color3){0.50f, 1.0f, 0.12f}, true);
        }

        /* Health Hearts */
        float y_hearts = y_exp - 10.0f;
        bool hurt = p->hurt_timer > 0.0f;
        for (int i = 0; i < 10; i++) {
            float hx = x_hotbar + (float)i * 8.0f;
            float hp_val = p->health - (float)i * 2.0f;

            if (r->gui_heart_empty_tex)
                renderer_sprite(r, r->gui_heart_empty_tex, hx, y_hearts, 9, 9);

            if (hp_val >= 2.0f) {
                GLuint tex = hurt ? (r->gui_heart_blink_tex ? r->gui_heart_blink_tex : r->gui_heart_full_tex)
                                  : r->gui_heart_full_tex;
                if (tex) renderer_sprite(r, tex, hx, y_hearts, 9, 9);
            } else if (hp_val >= 1.0f) {
                GLuint tex = hurt ? (r->gui_heart_half_blink_tex ? r->gui_heart_half_blink_tex : r->gui_heart_half_tex)
                                  : r->gui_heart_half_tex;
                if (tex) renderer_sprite(r, tex, hx, y_hearts, 9, 9);
            }
        }

        /* Armor Shields */
        if (p->armor_points > 0) {
            float y_armor = y_hearts - 9.0f;
            for (int i = 0; i < 10; i++) {
                float ax = x_hotbar + (float)i * 8.0f;
                float arm_val = (float)p->armor_points - (float)i * 2.0f;

                if (r->gui_armor_empty_tex)
                    renderer_sprite(r, r->gui_armor_empty_tex, ax, y_armor, 9, 9);

                if (arm_val >= 2.0f) {
                    if (r->gui_armor_full_tex)
                        renderer_sprite(r, r->gui_armor_full_tex, ax, y_armor, 9, 9);
                } else if (arm_val >= 1.0f) {
                    if (r->gui_armor_half_tex)
                        renderer_sprite(r, r->gui_armor_half_tex, ax, y_armor, 9, 9);
                }
            }
        }

        /* Food / Hunger drumsticks */
        float y_food = y_exp - 10.0f;
        for (int i = 0; i < 10; i++) {
            float fx = (cx + 91.0f - 9.0f) - (float)i * 8.0f;
            float fd_val = p->hunger - (float)i * 2.0f;

            if (r->gui_food_empty_tex)
                renderer_sprite(r, r->gui_food_empty_tex, fx, y_food, 9, 9);

            if (fd_val >= 2.0f) {
                if (r->gui_food_full_tex)
                    renderer_sprite(r, r->gui_food_full_tex, fx, y_food, 9, 9);
            } else if (fd_val >= 1.0f) {
                if (r->gui_food_half_tex)
                    renderer_sprite(r, r->gui_food_half_tex, fx, y_food, 9, 9);
            }
        }

        /* Air Bubbles */
        if (p->in_water || p->air < PLAYER_MAX_AIR) {
            float y_air = y_food - 9.0f;
            int bubbles = (int)ceilf((p->air / PLAYER_MAX_AIR) * 10.0f);
            if (bubbles < 0) bubbles = 0;
            if (bubbles > 10) bubbles = 10;
            for (int i = 0; i < bubbles; i++) {
                float bx = (cx + 91.0f - 9.0f) - (float)i * 8.0f;
                if (r->gui_air_tex)
                    renderer_sprite(r, r->gui_air_tex, bx, y_air, 9, 9);
            }
        }
    }
}

static void draw_debug(Renderer *r, UI *ui, Player *p, World *w, HUDInfo *info) {
    char lines[14][160];
    int n = 0;
#if MCED_USE_GL
    snprintf(lines[n++], sizeof(lines[0]), "Minecraft C Edition - OpenGL (Hardware)");
#else
    snprintf(lines[n++], sizeof(lines[0]), "Minecraft C Edition - TinyGL (Software)");
#endif
    snprintf(lines[n++], sizeof(lines[0]), "FPS: %.0f   Resolution: %dx%d", info->fps, r->width, r->height);
    snprintf(lines[n++], sizeof(lines[0]), "XYZ: %.2f / %.2f / %.2f",
             p->pos.x, p->pos.y, p->pos.z);
    snprintf(lines[n++], sizeof(lines[0]), "Chunk: %d, %d   seed: %llu",
             (int)floorf(p->pos.x / CHUNK_SIZE), (int)floorf(p->pos.z / CHUNK_SIZE),
             (unsigned long long)info->seed);
    Vec3 f = player_forward(p);
    const char *facing = fabsf(f.z) > fabsf(f.x) ? (f.z < 0 ? "north" : "south")
                                                  : (f.x < 0 ? "west" : "east");
    snprintf(lines[n++], sizeof(lines[0]), "Facing: %s  yaw %.1f  pitch %.1f",
             facing, p->yaw, p->pitch);
    snprintf(lines[n++], sizeof(lines[0]), "Loaded chunks: %d   meshes: %d",
             w->chunk_count, info->mesh_chunks);
    snprintf(lines[n++], sizeof(lines[0]), "Render distance: %d chunks (%dm)   Quads: %d",
             r->render_radius, r->render_radius * CHUNK_SIZE, info->quads);
    snprintf(lines[n++], sizeof(lines[0]), "Fly: %s   Water: %s",
             p->flying ? "on" : "off", p->in_water ? "yes" : "no");
    snprintf(lines[n++], sizeof(lines[0]), "Health: %.1f / %.0f   air: %.1f",
             p->health, PLAYER_MAX_HEALTH, p->air);
    if (p->target_valid) {
        snprintf(lines[n++], sizeof(lines[0]), "Looking at: %s (%d, %d, %d)",
                 g_block_defs[p->target_block].name,
                 p->target_x, p->target_y, p->target_z);
    } else {
        snprintf(lines[n++], sizeof(lines[0]), "Looking at: none");
    }

    float wpx = 0;
    for (int i = 0; i < n; i++) {
        int tw = renderer_text_width(r, 0.8f, lines[i]);
        if (tw > wpx) wpx = (float)tw;
    }
    renderer_rect(r, 4, 4, wpx + 8, n * 10 + 6, (Color3){0.06f, 0.06f, 0.08f});
    for (int i = 0; i < n; i++)
        renderer_text(r, 8, 7 + i * 10, 0.8f, lines[i], C_TEXT, false);
}

static void draw_survival_inventory(UI *ui, Renderer *r, Player *p) {
    renderer_rect(r, 0, 0, (float)ui->width, (float)ui->height, (Color3){0.0f, 0.0f, 0.0f});

    float wx = (float)((ui->width - 176) / 2);
    float wy = (float)((ui->height - 166) / 2);

    if (r->gui_inventory_tex) {
        renderer_sprite_uv(r, r->gui_inventory_tex, wx, wy, 176.0f, 166.0f,
                           0.0f, 0.0f, 176.0f / 256.0f, 166.0f / 256.0f);
    } else {
        renderer_rect(r, wx, wy, 176, 166, C_PANEL);
        renderer_rect_outline(r, wx, wy, 176, 166, 2, C_PANEL_HI);
    }

    renderer_text(r, wx + 86, wy + 6, 0.75f, "Crafting", (Color3){0.25f, 0.25f, 0.25f}, false);
    renderer_text(r, wx + 8, wy + 72, 0.75f, "Inventory", (Color3){0.25f, 0.25f, 0.25f}, false);

    /* Player preview doll */
    float px = wx + 26 + 18, py = wy + 8 + 14;
    renderer_rect(r, px, py, 14, 14, (Color3){0.78f, 0.60f, 0.45f});
    renderer_rect(r, px, py, 14, 4, (Color3){0.35f, 0.22f, 0.12f});
    renderer_rect(r, px + 2, py + 6, 2, 2, (Color3){0.25f, 0.25f, 0.75f});
    renderer_rect(r, px + 10, py + 6, 2, 2, (Color3){0.25f, 0.25f, 0.75f});
    renderer_rect(r, px - 3, py + 14, 20, 18, (Color3){0.0f, 0.65f, 0.65f});
    renderer_rect(r, px - 6, py + 14, 3, 18, (Color3){0.78f, 0.60f, 0.45f});
    renderer_rect(r, px + 17, py + 14, 3, 18, (Color3){0.78f, 0.60f, 0.45f});
    renderer_rect(r, px - 1, py + 32, 16, 16, (Color3){0.15f, 0.20f, 0.55f});
    renderer_rect(r, px - 1, py + 48, 16, 4, (Color3){0.30f, 0.30f, 0.32f});

    SlotType hover_type = SLOT_NONE;
    int hover_idx = -1;
    uint8_t hover_item = BLOCK_AIR;
    float mx = ui->mouse_x, my = ui->mouse_y;

    /* 1. Crafting grid (4 slots) */
    const float craft_pos[4][2] = {
        {wx + 98, wy + 18}, {wx + 116, wy + 18},
        {wx + 98, wy + 36}, {wx + 116, wy + 36}
    };
    for (int i = 0; i < 4; i++) {
        float sx = craft_pos[i][0], sy = craft_pos[i][1];
        if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
            hover_type = SLOT_CRAFTING;
            hover_idx = i;
            hover_item = p->crafting[i];
            renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
        }
        if (p->crafting[i] != BLOCK_AIR) {
            renderer_block_icon(r, p->crafting[i], sx, sy, 16);
            if (p->crafting_counts[i] > 1) {
                char s[8];
                snprintf(s, sizeof(s), "%d", p->crafting_counts[i]);
                renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
            }
        }
    }

    /* 2. Crafting result slot */
    float rx = wx + 154, ry = wy + 28;
    if (mx >= rx && mx < rx + 16 && my >= ry && my < ry + 16) {
        hover_type = SLOT_CRAFT_RESULT;
        hover_idx = 0;
        hover_item = p->craft_result;
        renderer_rect(r, rx, ry, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
    }
    if (p->craft_result != BLOCK_AIR) {
        renderer_block_icon(r, p->craft_result, rx, ry, 16);
        if (p->craft_result_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->craft_result_count);
            renderer_text(r, rx + 9, ry + 8, 0.75f, s, C_WHITE, true);
        }
    }

    /* 3. Armor slots (4) */
    for (int i = 0; i < 4; i++) {
        float sx = wx + 8, sy = wy + 8 + (float)i * 18.0f;
        if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
            hover_type = SLOT_ARMOR;
            hover_idx = i;
            hover_item = p->armor[i];
            renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
        }
        if (p->armor[i] != BLOCK_AIR) {
            renderer_block_icon(r, p->armor[i], sx, sy, 16);
        }
    }

    /* 4. Main Inventory (27 slots) */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 9; col++) {
            int idx = row * 9 + col;
            float sx = wx + 8 + (float)col * 18.0f;
            float sy = wy + 84 + (float)row * 18.0f;
            if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
                hover_type = SLOT_INVENTORY;
                hover_idx = idx;
                hover_item = p->inventory[idx];
                renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
            }
            if (p->inventory[idx] != BLOCK_AIR) {
                renderer_block_icon(r, p->inventory[idx], sx, sy, 16);
                if (p->inventory_counts[idx] > 1) {
                    char s[8];
                    snprintf(s, sizeof(s), "%d", p->inventory_counts[idx]);
                    renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
                }
            }
        }
    }

    /* 5. Hotbar (9 slots) */
    for (int col = 0; col < 9; col++) {
        float sx = wx + 8 + (float)col * 18.0f;
        float sy = wy + 142;
        if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
            hover_type = SLOT_HOTBAR;
            hover_idx = col;
            hover_item = p->hotbar[col];
            renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
        }
        if (p->hotbar[col] != BLOCK_AIR) {
            renderer_block_icon(r, p->hotbar[col], sx, sy, 16);
            if (p->hotbar_counts[col] > 1) {
                char s[8];
                snprintf(s, sizeof(s), "%d", p->hotbar_counts[col]);
                renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
            }
        }
    }

    ui->hovered_slot_type = hover_type;
    ui->hovered_slot_index = hover_idx;

    /* 6. Floating cursor item stack */
    if (p->cursor_item != BLOCK_AIR && p->cursor_count > 0) {
        renderer_block_icon(r, p->cursor_item, mx - 8, my - 8, 16);
        if (p->cursor_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->cursor_count);
            renderer_text(r, mx + 1, my + 1, 0.75f, s, C_WHITE, true);
        }
    } else if (hover_item != BLOCK_AIR) {
        const char *tname = g_block_defs[hover_item].name;
        float tw = (float)renderer_text_width(r, 0.85f, tname);
        float tx = mx + 10, ty = my - 12;
        if (tx + tw + 6 > (float)ui->width) tx = mx - tw - 10;
        renderer_rect(r, tx - 3, ty - 2, tw + 6, 14, (Color3){0.06f, 0.03f, 0.12f});
        renderer_rect_outline(r, tx - 3, ty - 2, tw + 6, 14, 1, (Color3){0.32f, 0.16f, 0.65f});
        renderer_text(r, tx, ty, 0.85f, tname, C_WHITE, true);
    }
}

static void draw_creative_inventory(UI *ui, Renderer *r, Player *p) {
    renderer_rect(r, 0, 0, (float)ui->width, (float)ui->height, (Color3){0.0f, 0.0f, 0.0f});

    float wx = (float)((ui->width - 195) / 2);
    float wy = (float)((ui->height - 136) / 2);

    if (r->gui_creative_tex) {
        renderer_sprite_uv(r, r->gui_creative_tex, wx, wy, 195.0f, 136.0f,
                           0.0f, 0.0f, 195.0f / 256.0f, 136.0f / 256.0f);
    } else {
        renderer_rect(r, wx, wy, 195, 136, C_PANEL);
        renderer_rect_outline(r, wx, wy, 195, 136, 2, C_PANEL_HI);
    }

    renderer_text(r, wx + 8, wy + 6, 0.75f, "Item Selection", (Color3){0.25f, 0.25f, 0.25f}, false);

    SlotType hover_type = SLOT_NONE;
    int hover_idx = -1;
    uint8_t hover_item = BLOCK_AIR;
    float mx = ui->mouse_x, my = ui->mouse_y;

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 9; col++) {
            int p_idx = ui->inv_page * 45 + row * 9 + col;
            float sx = wx + 9 + (float)col * 18.0f;
            float sy = wy + 18 + (float)row * 18.0f;
            if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
                hover_type = SLOT_CREATIVE_PALETTE;
                hover_idx = p_idx;
                if (p_idx < g_palette_count) hover_item = (uint8_t)g_palette[p_idx];
                renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
            }
            if (p_idx < g_palette_count) {
                renderer_block_icon(r, (uint8_t)g_palette[p_idx], sx, sy, 16);
            }
        }
    }

    for (int col = 0; col < 9; col++) {
        float sx = wx + 9 + (float)col * 18.0f;
        float sy = wy + 112;
        if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
            hover_type = SLOT_HOTBAR;
            hover_idx = col;
            hover_item = p->hotbar[col];
            renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
        }
        if (p->hotbar[col] != BLOCK_AIR) {
            renderer_block_icon(r, p->hotbar[col], sx, sy, 16);
        }
    }

    ui->hovered_slot_type = hover_type;
    ui->hovered_slot_index = hover_idx;

    int pages = (g_palette_count + 44) / 45;
    if (pages < 1) pages = 1;
    char page_str[32];
    snprintf(page_str, sizeof(page_str), "%d/%d", ui->inv_page + 1, pages);
    renderer_text(r, wx + 166, wy + 6, 0.70f, page_str, (Color3){0.35f, 0.35f, 0.35f}, false);

    if (hover_item != BLOCK_AIR) {
        const char *tname = g_block_defs[hover_item].name;
        float tw = (float)renderer_text_width(r, 0.85f, tname);
        float tx = mx + 10, ty = my - 12;
        if (tx + tw + 6 > (float)ui->width) tx = mx - tw - 10;
        renderer_rect(r, tx - 3, ty - 2, tw + 6, 14, (Color3){0.06f, 0.03f, 0.12f});
        renderer_rect_outline(r, tx - 3, ty - 2, tw + 6, 14, 1, (Color3){0.32f, 0.16f, 0.65f});
        renderer_text(r, tx, ty, 0.85f, tname, C_WHITE, true);
    }
}

void ui_chat_open(UI *ui, const char *initial_text) {
    ui->screen = UI_SCREEN_CHAT;
    ui->button_count = 0;
    ui->chat_input[0] = '\0';
    ui->chat_input_len = 0;
    if (initial_text) {
        snprintf(ui->chat_input, sizeof(ui->chat_input), "%s", initial_text);
        ui->chat_input_len = (int)strlen(ui->chat_input);
    }
}

void ui_chat_input_char(UI *ui, char c) {
    if (ui->screen != UI_SCREEN_CHAT)
        return;
    if (ui->chat_input_len + 1 < CHAT_INPUT_MAX - 1 && (unsigned char)c >= 32) {
        ui->chat_input[ui->chat_input_len++] = c;
        ui->chat_input[ui->chat_input_len] = '\0';
    }
}

void ui_chat_backspace(UI *ui) {
    if (ui->screen != UI_SCREEN_CHAT)
        return;
    if (ui->chat_input_len > 0) {
        ui->chat_input[--ui->chat_input_len] = '\0';
    }
}

void ui_chat_add(UI *ui, const char *msg, Color3 color) {
    if (!msg || !msg[0])
        return;
    if (ui->chat_msg_count < CHAT_MSG_MAX) {
        snprintf(ui->chat_msgs[ui->chat_msg_count].text, sizeof(ui->chat_msgs[0].text), "%s", msg);
        ui->chat_msgs[ui->chat_msg_count].time = 10.0f;
        ui->chat_msgs[ui->chat_msg_count].color = color;
        ui->chat_msg_count++;
    } else {
        for (int i = 0; i < CHAT_MSG_MAX - 1; i++)
            ui->chat_msgs[i] = ui->chat_msgs[i + 1];
        snprintf(ui->chat_msgs[CHAT_MSG_MAX - 1].text, sizeof(ui->chat_msgs[0].text), "%s", msg);
        ui->chat_msgs[CHAT_MSG_MAX - 1].time = 10.0f;
        ui->chat_msgs[CHAT_MSG_MAX - 1].color = color;
    }
}

void ui_chat_update(UI *ui, float dt) {
    for (int i = 0; i < ui->chat_msg_count; i++) {
        if (ui->chat_msgs[i].time > 0)
            ui->chat_msgs[i].time -= dt;
    }
}

static void draw_chat_history(Renderer *r, UI *ui, bool active) {
    float y = (float)ui->height - (active ? 34.0f : 42.0f);
    int shown = 0;
    for (int i = ui->chat_msg_count - 1; i >= 0 && shown < 8; i--) {
        ChatMessage *m = &ui->chat_msgs[i];
        if (!active && m->time <= 0.0f)
            continue;
        float tw = (float)renderer_text_width(r, 0.85f, m->text);
        renderer_rect(r, 6, y - 10, tw + 8, 12, (Color3){0.05f, 0.05f, 0.05f});
        renderer_text(r, 10, y - 9, 0.85f, m->text, m->color, true);
        y -= 13.0f;
        shown++;
    }
}

static void draw_chat_input_box(Renderer *r, UI *ui) {
    float y = (float)ui->height - 20.0f;
    float w = (float)ui->width - 12.0f;
    renderer_rect(r, 6, y, w, 16, (Color3){0.05f, 0.05f, 0.05f});
    renderer_rect_outline(r, 6, y, w, 16, 1.0f, (Color3){0.6f, 0.6f, 0.6f});

    char buf[160];
    bool cursor = ((int)(ui->anim * 4.0f)) & 1;
    snprintf(buf, sizeof(buf), "> %s%s", ui->chat_input, cursor ? "_" : " ");
    renderer_text(r, 10, y + 3, 0.90f, buf, (Color3){1.0f, 1.0f, 1.0f}, true);
}

static void draw_crafting_table(UI *ui, Renderer *r, Player *p) {
    renderer_rect(r, 0, 0, (float)ui->width, (float)ui->height, (Color3){0.0f, 0.0f, 0.0f});

    float wx = (float)((ui->width - 176) / 2);
    float wy = (float)((ui->height - 166) / 2);

    if (r->gui_crafting_tex) {
        renderer_sprite_uv(r, r->gui_crafting_tex, wx, wy, 176.0f, 166.0f,
                           0.0f, 0.0f, 176.0f / 256.0f, 166.0f / 256.0f);
    } else {
        renderer_rect(r, wx, wy, 176, 166, C_PANEL);
        renderer_rect_outline(r, wx, wy, 176, 166, 2, C_PANEL_HI);
    }

    renderer_text(r, wx + 28, wy + 6, 0.75f, "Crafting", (Color3){0.25f, 0.25f, 0.25f}, false);
    renderer_text(r, wx + 8, wy + 72, 0.75f, "Inventory", (Color3){0.25f, 0.25f, 0.25f}, false);

    SlotType hover_type = SLOT_NONE;
    int hover_idx = -1;
    uint8_t hover_item = BLOCK_AIR;
    float mx = ui->mouse_x, my = ui->mouse_y;

    /* 3x3 Crafting grid: top-left (wx + 30, wy + 17), step 18 */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            float sx = wx + 30 + (float)col * 18.0f;
            float sy = wy + 17 + (float)row * 18.0f;
            if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
                hover_type = SLOT_CRAFTING_3X3;
                hover_idx = idx;
                hover_item = p->crafting_3x3[idx];
                renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
            }
            if (p->crafting_3x3[idx] != BLOCK_AIR) {
                renderer_block_icon(r, p->crafting_3x3[idx], sx, sy, 16);
                if (p->crafting_3x3_counts[idx] > 1) {
                    char s[8];
                    snprintf(s, sizeof(s), "%d", p->crafting_3x3_counts[idx]);
                    renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
                }
            }
        }
    }

    /* Crafting result slot at (wx + 124, wy + 35) */
    float rx = wx + 124, ry = wy + 35;
    if (mx >= rx && mx < rx + 16 && my >= ry && my < ry + 16) {
        hover_type = SLOT_CRAFT_3X3_RESULT;
        hover_idx = 0;
        hover_item = p->craft_3x3_result;
        renderer_rect(r, rx, ry, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
    }
    if (p->craft_3x3_result != BLOCK_AIR) {
        renderer_block_icon(r, p->craft_3x3_result, rx, ry, 16);
        if (p->craft_3x3_result_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->craft_3x3_result_count);
            renderer_text(r, rx + 9, ry + 8, 0.75f, s, C_WHITE, true);
        }
    }

    /* Main Inventory (27 slots at wx + 8, wy + 84) */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 9; col++) {
            int idx = row * 9 + col;
            float sx = wx + 8 + (float)col * 18.0f;
            float sy = wy + 84 + (float)row * 18.0f;
            if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
                hover_type = SLOT_INVENTORY;
                hover_idx = idx;
                hover_item = p->inventory[idx];
                renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
            }
            if (p->inventory[idx] != BLOCK_AIR) {
                renderer_block_icon(r, p->inventory[idx], sx, sy, 16);
                if (p->inventory_counts[idx] > 1) {
                    char s[8];
                    snprintf(s, sizeof(s), "%d", p->inventory_counts[idx]);
                    renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
                }
            }
        }
    }

    /* Hotbar (9 slots at wx + 8, wy + 142) */
    for (int col = 0; col < 9; col++) {
        float sx = wx + 8 + (float)col * 18.0f;
        float sy = wy + 142;
        if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
            hover_type = SLOT_HOTBAR;
            hover_idx = col;
            hover_item = p->hotbar[col];
            renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
        }
        if (p->hotbar[col] != BLOCK_AIR) {
            renderer_block_icon(r, p->hotbar[col], sx, sy, 16);
            if (p->hotbar_counts[col] > 1) {
                char s[8];
                snprintf(s, sizeof(s), "%d", p->hotbar_counts[col]);
                renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
            }
        }
    }

    ui->hovered_slot_type = hover_type;
    ui->hovered_slot_index = hover_idx;

    /* Floating cursor item stack */
    if (p->cursor_item != BLOCK_AIR && p->cursor_count > 0) {
        renderer_block_icon(r, p->cursor_item, mx - 8, my - 8, 16);
        if (p->cursor_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->cursor_count);
            renderer_text(r, mx + 1, my + 1, 0.75f, s, C_WHITE, true);
        }
    } else if (hover_item != BLOCK_AIR) {
        const char *tname = g_block_defs[hover_item].name;
        float tw = (float)renderer_text_width(r, 0.85f, tname);
        float tx = mx + 10, ty = my - 12;
        if (tx + tw + 6 > (float)ui->width) tx = mx - tw - 10;
        renderer_rect(r, tx - 3, ty - 2, tw + 6, 14, (Color3){0.06f, 0.03f, 0.12f});
        renderer_rect_outline(r, tx - 3, ty - 2, tw + 6, 14, 1, (Color3){0.32f, 0.16f, 0.65f});
        renderer_text(r, tx, ty, 0.85f, tname, C_WHITE, true);
    }
}

static void draw_furnace(UI *ui, Renderer *r, Player *p) {
    renderer_rect(r, 0, 0, (float)ui->width, (float)ui->height, (Color3){0.0f, 0.0f, 0.0f});

    float wx = (float)((ui->width - 176) / 2);
    float wy = (float)((ui->height - 166) / 2);

    if (r->gui_furnace_tex) {
        renderer_sprite_uv(r, r->gui_furnace_tex, wx, wy, 176.0f, 166.0f,
                           0.0f, 0.0f, 176.0f / 256.0f, 166.0f / 256.0f);
    } else {
        renderer_rect(r, wx, wy, 176, 166, C_PANEL);
        renderer_rect_outline(r, wx, wy, 176, 166, 2, C_PANEL_HI);
    }

    renderer_text(r, wx + 60, wy + 6, 0.75f, "Furnace", (Color3){0.25f, 0.25f, 0.25f}, false);
    renderer_text(r, wx + 8, wy + 72, 0.75f, "Inventory", (Color3){0.25f, 0.25f, 0.25f}, false);

    /* Animated lit flame (14x14 at wx + 56, wy + 36) */
    if (p->furnace_burn_time > 0.0f && r->gui_furnace_lit_tex) {
        float max_b = p->furnace_max_burn_time > 0.0f ? p->furnace_max_burn_time : 1.0f;
        float frac = p->furnace_burn_time / max_b;
        if (frac > 1.0f) frac = 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        float fh = roundf(14.0f * frac);
        if (fh > 0.0f) {
            float v0 = (14.0f - fh) / 14.0f;
            renderer_sprite_uv(r, r->gui_furnace_lit_tex, wx + 56, wy + 36 + (14.0f - fh), 14.0f, fh,
                               0.0f, v0, 1.0f, 1.0f);
        }
    }

    /* Animated burn progress arrow (24x16 at wx + 79, wy + 34) */
    if (p->furnace_cook_time > 0.0f && r->gui_furnace_burn_tex) {
        float frac = p->furnace_cook_time / 10.0f;
        if (frac > 1.0f) frac = 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        float aw = roundf(24.0f * frac);
        if (aw > 0.0f) {
            float u1 = aw / 24.0f;
            renderer_sprite_uv(r, r->gui_furnace_burn_tex, wx + 79, wy + 34, aw, 16.0f,
                               0.0f, 0.0f, u1, 1.0f);
        }
    }

    SlotType hover_type = SLOT_NONE;
    int hover_idx = -1;
    uint8_t hover_item = BLOCK_AIR;
    float mx = ui->mouse_x, my = ui->mouse_y;

    /* Input slot at (wx + 56, wy + 17) */
    float ix = wx + 56, iy = wy + 17;
    if (mx >= ix && mx < ix + 16 && my >= iy && my < iy + 16) {
        hover_type = SLOT_FURNACE_INPUT;
        hover_idx = 0;
        hover_item = p->furnace_input;
        renderer_rect(r, ix, iy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
    }
    if (p->furnace_input != BLOCK_AIR) {
        renderer_block_icon(r, p->furnace_input, ix, iy, 16);
        if (p->furnace_input_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->furnace_input_count);
            renderer_text(r, ix + 9, iy + 8, 0.75f, s, C_WHITE, true);
        }
    }

    /* Fuel slot at (wx + 56, wy + 53) */
    float fx = wx + 56, fy = wy + 53;
    if (mx >= fx && mx < fx + 16 && my >= fy && my < fy + 16) {
        hover_type = SLOT_FURNACE_FUEL;
        hover_idx = 0;
        hover_item = p->furnace_fuel;
        renderer_rect(r, fx, fy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
    }
    if (p->furnace_fuel != BLOCK_AIR) {
        renderer_block_icon(r, p->furnace_fuel, fx, fy, 16);
        if (p->furnace_fuel_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->furnace_fuel_count);
            renderer_text(r, fx + 9, fy + 8, 0.75f, s, C_WHITE, true);
        }
    }

    /* Output slot at (wx + 116, wy + 35) */
    float ox = wx + 116, oy = wy + 35;
    if (mx >= ox && mx < ox + 16 && my >= oy && my < oy + 16) {
        hover_type = SLOT_FURNACE_OUTPUT;
        hover_idx = 0;
        hover_item = p->furnace_output;
        renderer_rect(r, ox, oy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
    }
    if (p->furnace_output != BLOCK_AIR) {
        renderer_block_icon(r, p->furnace_output, ox, oy, 16);
        if (p->furnace_output_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->furnace_output_count);
            renderer_text(r, ox + 9, oy + 8, 0.75f, s, C_WHITE, true);
        }
    }

    /* Main Inventory (27 slots at wx + 8, wy + 84) */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 9; col++) {
            int idx = row * 9 + col;
            float sx = wx + 8 + (float)col * 18.0f;
            float sy = wy + 84 + (float)row * 18.0f;
            if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
                hover_type = SLOT_INVENTORY;
                hover_idx = idx;
                hover_item = p->inventory[idx];
                renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
            }
            if (p->inventory[idx] != BLOCK_AIR) {
                renderer_block_icon(r, p->inventory[idx], sx, sy, 16);
                if (p->inventory_counts[idx] > 1) {
                    char s[8];
                    snprintf(s, sizeof(s), "%d", p->inventory_counts[idx]);
                    renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
                }
            }
        }
    }

    /* Hotbar (9 slots at wx + 8, wy + 142) */
    for (int col = 0; col < 9; col++) {
        float sx = wx + 8 + (float)col * 18.0f;
        float sy = wy + 142;
        if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
            hover_type = SLOT_HOTBAR;
            hover_idx = col;
            hover_item = p->hotbar[col];
            renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
        }
        if (p->hotbar[col] != BLOCK_AIR) {
            renderer_block_icon(r, p->hotbar[col], sx, sy, 16);
            if (p->hotbar_counts[col] > 1) {
                char s[8];
                snprintf(s, sizeof(s), "%d", p->hotbar_counts[col]);
                renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
            }
        }
    }

    ui->hovered_slot_type = hover_type;
    ui->hovered_slot_index = hover_idx;

    /* Floating cursor item stack */
    if (p->cursor_item != BLOCK_AIR && p->cursor_count > 0) {
        renderer_block_icon(r, p->cursor_item, mx - 8, my - 8, 16);
        if (p->cursor_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->cursor_count);
            renderer_text(r, mx + 1, my + 1, 0.75f, s, C_WHITE, true);
        }
    } else if (hover_item != BLOCK_AIR) {
        const char *tname = g_block_defs[hover_item].name;
        float tw = (float)renderer_text_width(r, 0.85f, tname);
        float tx = mx + 10, ty = my - 12;
        if (tx + tw + 6 > (float)ui->width) tx = mx - tw - 10;
        renderer_rect(r, tx - 3, ty - 2, tw + 6, 14, (Color3){0.06f, 0.03f, 0.12f});
        renderer_rect_outline(r, tx - 3, ty - 2, tw + 6, 14, 1, (Color3){0.32f, 0.16f, 0.65f});
        renderer_text(r, tx, ty, 0.85f, tname, C_WHITE, true);
    }
}

static void draw_chest(UI *ui, Renderer *r, Player *p) {
    renderer_rect(r, 0, 0, (float)ui->width, (float)ui->height, (Color3){0.0f, 0.0f, 0.0f});

    float wx = (float)((ui->width - 176) / 2);
    float wy = (float)((ui->height - 166) / 2);

    if (r->gui_chest_tex) {
        renderer_sprite_uv(r, r->gui_chest_tex, wx, wy, 176.0f, 166.0f,
                           0.0f, 0.0f, 176.0f / 256.0f, 166.0f / 256.0f);
    } else {
        renderer_rect(r, wx, wy, 176, 166, C_PANEL);
        renderer_rect_outline(r, wx, wy, 176, 166, 2, C_PANEL_HI);
    }

    renderer_text(r, wx + 8, wy + 6, 0.75f, "Chest", (Color3){0.25f, 0.25f, 0.25f}, false);
    renderer_text(r, wx + 8, wy + 72, 0.75f, "Inventory", (Color3){0.25f, 0.25f, 0.25f}, false);

    SlotType hover_type = SLOT_NONE;
    int hover_idx = -1;
    uint8_t hover_item = BLOCK_AIR;
    float mx = ui->mouse_x, my = ui->mouse_y;

    /* 27 Chest Slots at (wx + 8, wy + 18) */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 9; col++) {
            int idx = row * 9 + col;
            float sx = wx + 8 + (float)col * 18.0f;
            float sy = wy + 18 + (float)row * 18.0f;
            if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
                hover_type = SLOT_CHEST;
                hover_idx = idx;
                hover_item = p->chest[idx];
                renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
            }
            if (p->chest[idx] != BLOCK_AIR) {
                renderer_block_icon(r, p->chest[idx], sx, sy, 16);
                if (p->chest_counts[idx] > 1) {
                    char s[8];
                    snprintf(s, sizeof(s), "%d", p->chest_counts[idx]);
                    renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
                }
            }
        }
    }

    /* Main Inventory (27 slots at wx + 8, wy + 84) */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 9; col++) {
            int idx = row * 9 + col;
            float sx = wx + 8 + (float)col * 18.0f;
            float sy = wy + 84 + (float)row * 18.0f;
            if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
                hover_type = SLOT_INVENTORY;
                hover_idx = idx;
                hover_item = p->inventory[idx];
                renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
            }
            if (p->inventory[idx] != BLOCK_AIR) {
                renderer_block_icon(r, p->inventory[idx], sx, sy, 16);
                if (p->inventory_counts[idx] > 1) {
                    char s[8];
                    snprintf(s, sizeof(s), "%d", p->inventory_counts[idx]);
                    renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
                }
            }
        }
    }

    /* Hotbar (9 slots at wx + 8, wy + 142) */
    for (int col = 0; col < 9; col++) {
        float sx = wx + 8 + (float)col * 18.0f;
        float sy = wy + 142;
        if (mx >= sx && mx < sx + 16 && my >= sy && my < sy + 16) {
            hover_type = SLOT_HOTBAR;
            hover_idx = col;
            hover_item = p->hotbar[col];
            renderer_rect(r, sx, sy, 16, 16, (Color3){0.9f, 0.9f, 0.9f});
        }
        if (p->hotbar[col] != BLOCK_AIR) {
            renderer_block_icon(r, p->hotbar[col], sx, sy, 16);
            if (p->hotbar_counts[col] > 1) {
                char s[8];
                snprintf(s, sizeof(s), "%d", p->hotbar_counts[col]);
                renderer_text(r, sx + 9, sy + 8, 0.75f, s, C_WHITE, true);
            }
        }
    }

    ui->hovered_slot_type = hover_type;
    ui->hovered_slot_index = hover_idx;

    /* Floating cursor item stack */
    if (p->cursor_item != BLOCK_AIR && p->cursor_count > 0) {
        renderer_block_icon(r, p->cursor_item, mx - 8, my - 8, 16);
        if (p->cursor_count > 1) {
            char s[8];
            snprintf(s, sizeof(s), "%d", p->cursor_count);
            renderer_text(r, mx + 1, my + 1, 0.75f, s, C_WHITE, true);
        }
    } else if (hover_item != BLOCK_AIR) {
        const char *tname = g_block_defs[hover_item].name;
        float tw = (float)renderer_text_width(r, 0.85f, tname);
        float tx = mx + 10, ty = my - 12;
        if (tx + tw + 6 > (float)ui->width) tx = mx - tw - 10;
        renderer_rect(r, tx - 3, ty - 2, tw + 6, 14, (Color3){0.06f, 0.03f, 0.12f});
        renderer_rect_outline(r, tx - 3, ty - 2, tw + 6, 14, 1, (Color3){0.32f, 0.16f, 0.65f});
        renderer_text(r, tx, ty, 0.85f, tname, C_WHITE, true);
    }
}

void ui_draw(Renderer *r, UI *ui, Player *p, World *w, HUDInfo *info) {
    renderer_ui_begin(r);
    ui->anim += 0.016f;

    switch (ui->screen) {
    case UI_SCREEN_TITLE:
        draw_title(ui, r);
        return;
    case UI_SCREEN_CREATE_WORLD:
        draw_create_world(ui, r);
        return;
    case UI_SCREEN_PAUSE:
        draw_pause(ui, r, p);
        return;
    case UI_SCREEN_OPTIONS:
        draw_options(ui, r);
        return;
    case UI_SCREEN_VIDEO:
        draw_video(ui, r);
        return;
    case UI_SCREEN_AUDIO:
        draw_audio(ui, r);
        return;
    case UI_SCREEN_CONTROLS:
        draw_controls(ui, r);
        return;
    case UI_SCREEN_ADVANCEMENTS:
        draw_advancements(ui, r);
        return;
    case UI_SCREEN_INVENTORY:
        if (p && p->creative)
            draw_creative_inventory(ui, r, p);
        else if (p)
            draw_survival_inventory(ui, r, p);
        return;
    case UI_SCREEN_CRAFTING:
        if (p)
            draw_crafting_table(ui, r, p);
        return;
    case UI_SCREEN_FURNACE:
        if (p)
            draw_furnace(ui, r, p);
        return;
    case UI_SCREEN_CHEST:
        if (p)
            draw_chest(ui, r, p);
        return;
    case UI_SCREEN_DEATH:
        draw_death(ui, r);
        return;
    case UI_SCREEN_CHAT:
        draw_chat_history(r, ui, true);
        draw_chat_input_box(r, ui);
        if (p) draw_hud(r, ui, p);
        return;
    default:
        break;
    }

    if (ui->screen != UI_SCREEN_NONE)
        return;

    draw_crosshair(r, ui);
    if (p) draw_hud(r, ui, p);

    draw_chat_history(r, ui, false);

    if (info->toast && info->toast_time > 0) {
        renderer_text_centered(r, ui->width * 0.5f, ui->height - 60, 0.95f,
                               info->toast, C_YELLOW, true);
    }
    if (info->show_debug && p)
        draw_debug(r, ui, p, w, info);
}

void ui_draw_loading(Renderer *r, const char *status, float progress) {
    renderer_ui_begin(r);
    float uw = (float)(r->ui_width > 0 ? r->ui_width : r->width);
    float uh = (float)(r->ui_height > 0 ? r->ui_height : r->height);
    renderer_dirt_background(r);

    renderer_text_centered(r, uw * 0.5f, uh * 0.5f - 40, 2.2f, "Loading world", C_WHITE, true);

    char desc[128];
    if (status && status[0])
        snprintf(desc, sizeof(desc), "%s", status);
    else
        snprintf(desc, sizeof(desc), "Building terrain");
    renderer_text_centered(r, uw * 0.5f, uh * 0.5f - 14, 1.0f, desc, (Color3){0.8f, 0.8f, 0.8f}, true);

    float bw = 200, bx = (uw - bw) * 0.5f, by = uh * 0.5f + 16;
    renderer_rect(r, bx - 2, by - 2, bw + 4, 10, (Color3){0.04f, 0.04f, 0.05f});
    renderer_rect(r, bx, by, bw, 6, (Color3){0.20f, 0.20f, 0.22f});
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    renderer_rect(r, bx, by, bw * progress, 6, (Color3){0.50f, 1.0f, 0.12f});

    char pct_str[32];
    snprintf(pct_str, sizeof(pct_str), "%d%%", (int)(progress * 100.0f));
    renderer_text_centered(r, uw * 0.5f, by + 14, 0.85f, pct_str, (Color3){0.7f, 0.7f, 0.7f}, true);
}

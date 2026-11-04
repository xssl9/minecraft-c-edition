#ifndef MCED_UI_H
#define MCED_UI_H

#include "mced.h"
#include "player.h"
#include "render.h"
#include "settings.h"
#include "world.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_SCREEN_NONE = 0,
    UI_SCREEN_TITLE,
    UI_SCREEN_CREATE_WORLD,
    UI_SCREEN_PAUSE,
    UI_SCREEN_OPTIONS,
    UI_SCREEN_VIDEO,
    UI_SCREEN_AUDIO,
    UI_SCREEN_CONTROLS,
    UI_SCREEN_ADVANCEMENTS,
    UI_SCREEN_INVENTORY,
    UI_SCREEN_CRAFTING,
    UI_SCREEN_FURNACE,
    UI_SCREEN_CHEST,
    UI_SCREEN_DEATH,
    UI_SCREEN_LOADING,
    UI_SCREEN_CHAT
} UIScreen;

typedef enum {
    UI_ACT_NONE = 0,
    UI_ACT_CONTINUE,
    UI_ACT_NEW_WORLD,
    UI_ACT_QUIT,
    UI_ACT_RESUME,
    UI_ACT_SAVE_QUIT,
    UI_ACT_INV_CLOSE,
    UI_ACT_INV_PREV,
    UI_ACT_INV_NEXT,
    UI_ACT_PICK_BLOCK,
    UI_ACT_RESPAWN,
    UI_ACT_DEATH_TITLE,

    /* Create World Actions */
    UI_ACT_CREATE_WORLD_START,
    UI_ACT_CREATE_WORLD_CANCEL,
    UI_ACT_TOGGLE_GAMEMODE,
    UI_ACT_TOGGLE_DIFFICULTY,
    UI_ACT_TOGGLE_CHEATS,
    UI_ACT_MORE_WORLD_OPTIONS,
    UI_ACT_TOGGLE_WORLD_TYPE,

    /* Settings Navigation */
    UI_ACT_OPEN_OPTIONS,
    UI_ACT_OPEN_VIDEO,
    UI_ACT_OPEN_AUDIO,
    UI_ACT_OPEN_CONTROLS,
    UI_ACT_OPEN_ADVANCEMENTS,
    UI_ACT_DONE,

    /* Settings Toggles */
    UI_ACT_TOGGLE_VSYNC,
    UI_ACT_TOGGLE_BOBBING,
    UI_ACT_TOGGLE_CLOUDS,
    UI_ACT_TOGGLE_FOG,
    UI_ACT_TOGGLE_FULLSCREEN,
    UI_ACT_TOGGLE_MUTE,
    UI_ACT_TOGGLE_INVERT_MOUSE,
    UI_ACT_CYCLE_GUI_SCALE
} UIAction;

typedef enum {
    WIDGET_BUTTON = 0,
    WIDGET_SLIDER
} WidgetType;

typedef enum {
    SLIDER_NONE = 0,
    SLIDER_FOV,
    SLIDER_RENDER_DISTANCE,
    SLIDER_MAX_FPS,
    SLIDER_BRIGHTNESS,
    SLIDER_MASTER_VOL,
    SLIDER_MUSIC_VOL,
    SLIDER_BLOCKS_VOL,
    SLIDER_SENSITIVITY
} SliderID;

typedef struct {
    char label[64];
    UIAction action;
    WidgetType type;
    SliderID slider_id;
    float slider_val;   /* 0.0 to 1.0 */
    float x, y, w, h;
    bool disabled;
} UIButton;

typedef struct {
    float fps;
    int loaded_chunks;
    int mesh_chunks;
    int quads;
    uint64_t seed;
    float render_x, render_z;
    bool show_debug;
    const char *toast;
    float toast_time;
} HUDInfo;

#define CHAT_MSG_MAX 16
#define CHAT_INPUT_MAX 128
#define UI_MAX_BUTTONS 16

typedef struct {
    char text[128];
    float time;
    Color3 color;
} ChatMessage;

typedef struct {
    UIScreen screen;
    UIScreen prev_screen;
    int width, height;
    UIButton buttons[UI_MAX_BUTTONS];
    int button_count;
    int hover;
    int active_slider;
    float mouse_x, mouse_y;
    bool has_save;
    int inv_page;
    int clicked_block;
    bool show_debug;
    float anim;

    Settings *settings;

    /* Create World State */
    char world_name[64];
    int world_name_len;
    char world_seed[64];
    int world_seed_len;
    int focused_text_field; /* 0 = none, 1 = world_name, 2 = world_seed */
    int create_gamemode;    /* 0 = Survival, 1 = Creative, 2 = Hardcore */
    int create_difficulty;  /* 0 = Peaceful, 1 = Easy, 2 = Normal, 3 = Hard */
    bool create_cheats;
    bool show_more_options;
    int create_world_type;  /* 0 = Default, 1 = Superflat */

    /* Inventory State */
    int active_inv_tab;     /* 0 = Building, 1 = Decoration, 2 = Redstone, 3 = Natural, 4 = Survival */
    SlotType hovered_slot_type;
    int hovered_slot_index;

    char chat_input[CHAT_INPUT_MAX];
    int chat_input_len;
    ChatMessage chat_msgs[CHAT_MSG_MAX];
    int chat_msg_count;
} UI;

void ui_init(UI *ui, int width, int height);
void ui_set_settings(UI *ui, Settings *s);
void ui_set_screen(UI *ui, UIScreen screen, bool has_save);
void ui_mouse_move(UI *ui, float x, float y);
bool ui_mouse_down(UI *ui, float x, float y);
void ui_mouse_up(UI *ui);
void ui_scroll_inventory(UI *ui, int direction);
UIAction ui_click(UI *ui, float x, float y, Player *p);
void ui_refresh_button_labels(UI *ui);

void ui_slot_click_event(UI *ui, Player *p, bool right_click, bool shift_click);
void ui_text_input_char(UI *ui, char c);
void ui_text_input_backspace(UI *ui);

void ui_chat_open(UI *ui, const char *initial_text);
void ui_chat_input_char(UI *ui, char c);
void ui_chat_backspace(UI *ui);
void ui_chat_add(UI *ui, const char *msg, Color3 color);
void ui_chat_update(UI *ui, float dt);

void ui_draw(Renderer *r, UI *ui, Player *p, World *w, HUDInfo *info);
void ui_draw_loading(Renderer *r, const char *status, float progress);

#endif

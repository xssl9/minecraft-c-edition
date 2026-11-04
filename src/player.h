#ifndef MCED_PLAYER_H
#define MCED_PLAYER_H

#include "mced.h"
#include "world.h"

#include <stdbool.h>
#include <stdint.h>

#define PLAYER_WIDTH 0.6f
#define PLAYER_HEIGHT 1.8f
#define PLAYER_EYE 1.62f
#define PLAYER_MAX_HEALTH 20.0f
#define PLAYER_MAX_AIR 15.0f
#define PLAYER_MAX_HUNGER 20.0f
#define HOTBAR_SLOTS 9
#define INVENTORY_SLOTS 27
#define ARMOR_SLOTS 4
#define CRAFTING_SLOTS 4
#define CRAFTING_3X3_SLOTS 9
#define CHEST_SLOTS 27

typedef enum {
    SLOT_NONE = 0,
    SLOT_HOTBAR,
    SLOT_INVENTORY,
    SLOT_ARMOR,
    SLOT_CRAFTING,
    SLOT_CRAFT_RESULT,
    SLOT_CREATIVE_PALETTE,
    SLOT_OFFHAND,
    SLOT_CRAFTING_3X3,
    SLOT_CRAFT_3X3_RESULT,
    SLOT_FURNACE_INPUT,
    SLOT_FURNACE_FUEL,
    SLOT_FURNACE_OUTPUT,
    SLOT_CHEST
} SlotType;

typedef struct {
    bool forward, back, left, right;
    bool jump, sneak, sprint;
} PlayerInput;

typedef struct {
    Vec3 pos;             /* feet centre */
    Vec3 vel;
    float yaw, pitch;     /* degrees; pitch positive = up */
    bool on_ground;
    bool flying;
    bool creative;
    bool in_water;
    bool was_in_water;
    bool in_lava;
    float bob;
    float health;
    float air;
    float hunger;         /* 0.0 to 20.0, default 20.0 */
    float saturation;     /* 0.0 to 20.0, default 5.0 */
    float exhaustion;     /* 0.0 to 4.0 */
    int armor_points;     /* 0 to 20 */
    int level;            /* experience level */
    float exp_progress;   /* 0.0 to 1.0 */
    float fall_distance;
    float hurt_timer;
    float regen_timer;
    bool dead;
    float break_cooldown;
    float place_cooldown;
    float swing_timer;
    float mining_progress;
    int mining_x, mining_y, mining_z;
    float mining_sound_timer;

    /* Hotbar, Inventory, Armor, Crafting */
    int selected;
    uint8_t hotbar[HOTBAR_SLOTS];
    uint8_t hotbar_counts[HOTBAR_SLOTS];
    uint8_t inventory[INVENTORY_SLOTS];
    uint8_t inventory_counts[INVENTORY_SLOTS];
    uint8_t armor[ARMOR_SLOTS];
    uint8_t crafting[CRAFTING_SLOTS];
    uint8_t crafting_counts[CRAFTING_SLOTS];
    uint8_t craft_result;
    uint8_t craft_result_count;
    uint8_t offhand;
    uint8_t cursor_item;
    uint8_t cursor_count;

    /* 3x3 Crafting Table */
    uint8_t crafting_3x3[CRAFTING_3X3_SLOTS];
    uint8_t crafting_3x3_counts[CRAFTING_3X3_SLOTS];
    uint8_t craft_3x3_result;
    uint8_t craft_3x3_result_count;

    /* Furnace state */
    uint8_t furnace_input;
    uint8_t furnace_input_count;
    uint8_t furnace_fuel;
    uint8_t furnace_fuel_count;
    uint8_t furnace_output;
    uint8_t furnace_output_count;
    float furnace_burn_time;
    float furnace_max_burn_time;
    float furnace_cook_time;

    /* Chest state */
    uint8_t chest[CHEST_SLOTS];
    uint8_t chest_counts[CHEST_SLOTS];

    int target_x, target_y, target_z;
    bool target_valid;
    int target_nx, target_ny, target_nz;
    uint8_t target_block;
} Player;

void player_init(Player *p, Vec3 spawn);
void player_update(Player *p, World *w, const PlayerInput *in, float dt);
void player_damage(Player *p, float amount);
void player_respawn(Player *p, Vec3 spawn);
void player_update_armor_points(Player *p);
Vec3 player_eye(const Player *p);
Vec3 player_forward(const Player *p);

void player_update_crafting(Player *p);
void player_update_crafting_3x3(Player *p);
void player_update_furnace(Player *p, float dt);
void player_slot_click(Player *p, SlotType type, int index, bool right_click, bool shift_click);
void player_slot_click_crafting_3x3(Player *p, SlotType type, int index, bool right_click, bool shift_click);
void player_slot_click_furnace(Player *p, SlotType type, int index, bool right_click, bool shift_click);
void player_slot_click_chest(Player *p, SlotType type, int index, bool right_click, bool shift_click);

bool player_raycast(Player *p, World *w, float reach);
bool player_raycast_ex(Player *p, World *w, float reach, bool hit_liquids);
int player_break(Player *p, World *w);
bool player_place(Player *p, World *w, uint8_t block);

bool player_aabb_intersects_block(const Player *p, int bx, int by, int bz);

#endif

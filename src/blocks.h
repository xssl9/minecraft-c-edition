#ifndef MCED_BLOCKS_H
#define MCED_BLOCKS_H

#include "assets.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BLOCK_AIR = 0,
    BLOCK_STONE,
    BLOCK_GRANITE,
    BLOCK_DIORITE,
    BLOCK_ANDESITE,
    BLOCK_DEEPSLATE,
    BLOCK_COBBLESTONE,
    BLOCK_COBBLED_DEEPSLATE,
    BLOCK_DIRT,
    BLOCK_COARSE_DIRT,
    BLOCK_GRASS_BLOCK,
    BLOCK_PODZOL,
    BLOCK_MYCELIUM,
    BLOCK_SAND,
    BLOCK_RED_SAND,
    BLOCK_GRAVEL,
    BLOCK_CLAY,
    BLOCK_SANDSTONE,
    BLOCK_RED_SANDSTONE,
    BLOCK_SNOW_BLOCK,
    BLOCK_ICE,
    BLOCK_PACKED_ICE,
    BLOCK_BEDROCK,
    BLOCK_COAL_ORE,
    BLOCK_IRON_ORE,
    BLOCK_COPPER_ORE,
    BLOCK_GOLD_ORE,
    BLOCK_REDSTONE_ORE,
    BLOCK_LAPIS_ORE,
    BLOCK_DIAMOND_ORE,
    BLOCK_EMERALD_ORE,
    BLOCK_OAK_LOG,
    BLOCK_BIRCH_LOG,
    BLOCK_SPRUCE_LOG,
    BLOCK_JUNGLE_LOG,
    BLOCK_ACACIA_LOG,
    BLOCK_DARK_OAK_LOG,
    BLOCK_OAK_LEAVES,
    BLOCK_BIRCH_LEAVES,
    BLOCK_SPRUCE_LEAVES,
    BLOCK_JUNGLE_LEAVES,
    BLOCK_ACACIA_LEAVES,
    BLOCK_DARK_OAK_LEAVES,
    BLOCK_OAK_PLANKS,
    BLOCK_BIRCH_PLANKS,
    BLOCK_SPRUCE_PLANKS,
    BLOCK_JUNGLE_PLANKS,
    BLOCK_ACACIA_PLANKS,
    BLOCK_DARK_OAK_PLANKS,
    BLOCK_GLASS,
    BLOCK_BRICKS,
    BLOCK_STONE_BRICKS,
    BLOCK_MOSSY_COBBLESTONE,
    BLOCK_OBSIDIAN,
    BLOCK_NETHERRACK,
    BLOCK_SOUL_SAND,
    BLOCK_GLOWSTONE,
    BLOCK_SEA_LANTERN,
    BLOCK_CRAFTING_TABLE,
    BLOCK_FURNACE,
    BLOCK_CHEST,
    BLOCK_BOOKSHELF,
    BLOCK_WHITE_WOOL,
    BLOCK_RED_WOOL,
    BLOCK_ORANGE_WOOL,
    BLOCK_YELLOW_WOOL,
    BLOCK_LIME_WOOL,
    BLOCK_BLUE_WOOL,
    BLOCK_PURPLE_WOOL,
    BLOCK_BLACK_WOOL,
    BLOCK_CACTUS,
    BLOCK_PUMPKIN,
    BLOCK_CARVED_PUMPKIN,
    BLOCK_JACK_O_LANTERN,
    BLOCK_MELON,
    BLOCK_HAY_BLOCK,
    BLOCK_SPONGE,
    BLOCK_SLIME_BLOCK,
    BLOCK_HONEY_BLOCK,
    BLOCK_MOSS_BLOCK,
    BLOCK_CALCITE,
    BLOCK_TUFF,
    BLOCK_AMETHYST_BLOCK,
    BLOCK_COPPER_BLOCK,
    BLOCK_IRON_BLOCK,
    BLOCK_GOLD_BLOCK,
    BLOCK_DIAMOND_BLOCK,
    BLOCK_EMERALD_BLOCK,
    BLOCK_LAPIS_BLOCK,
    BLOCK_REDSTONE_BLOCK,
    BLOCK_COAL_BLOCK,
    BLOCK_QUARTZ_BLOCK,
    BLOCK_PURPUR_BLOCK,
    BLOCK_END_STONE,
    BLOCK_PRISMARINE,
    BLOCK_DARK_PRISMARINE,
    BLOCK_MUSHROOM_STEM,
    BLOCK_RED_MUSHROOM_BLOCK,
    BLOCK_BROWN_MUSHROOM_BLOCK,
    BLOCK_WATER,
    BLOCK_LAVA,
    BLOCK_TNT,
    BLOCK_TORCH,
    BLOCK_OAK_SLAB,
    BLOCK_COBBLESTONE_SLAB,
    BLOCK_STONE_SLAB,
    BLOCK_STONE_BRICK_SLAB,
    BLOCK_OAK_STAIRS,
    BLOCK_COBBLESTONE_STAIRS,
    BLOCK_STONE_BRICK_STAIRS,
    BLOCK_REDSTONE_WIRE,
    BLOCK_REDSTONE_TORCH,
    BLOCK_LEVER,
    BLOCK_STONE_BUTTON,
    BLOCK_REDSTONE_LAMP_OFF,
    BLOCK_REDSTONE_LAMP_ON,
    BLOCK_OAK_DOOR,
    BLOCK_OAK_DOOR_OPEN,
    BLOCK_IRON_DOOR,
    BLOCK_IRON_DOOR_OPEN,
    BLOCK_OAK_TRAPDOOR,
    BLOCK_OAK_TRAPDOOR_OPEN,
    BLOCK_DANDELION,
    BLOCK_POPPY,
    BLOCK_SHORT_GRASS,
    BLOCK_SUGAR_CANE,
    BLOCK_DEAD_BUSH,

    /* Crafting & Resource Items */
    ITEM_STICK,
    ITEM_COAL,
    ITEM_CHARCOAL,
    ITEM_RAW_IRON,
    ITEM_IRON_INGOT,
    ITEM_RAW_GOLD,
    ITEM_GOLD_INGOT,
    ITEM_DIAMOND,
    ITEM_EMERALD,
    ITEM_RAW_COPPER,
    ITEM_LAPIS_LAZULI,
    ITEM_REDSTONE,
    ITEM_BONE,
    ITEM_ARROW,
    ITEM_BOW,
    ITEM_ROTTEN_FLESH,
    ITEM_BUCKET,
    ITEM_WATER_BUCKET,
    ITEM_LAVA_BUCKET,
    ITEM_MILK_BUCKET,
    ITEM_WHEAT_SEEDS,
    ITEM_SUGAR,
    ITEM_PAPER,

    /* Armor */
    ITEM_IRON_HELMET,
    ITEM_IRON_CHESTPLATE,
    ITEM_IRON_LEGGINGS,
    ITEM_IRON_BOOTS,
    ITEM_DIAMOND_HELMET,
    ITEM_DIAMOND_CHESTPLATE,
    ITEM_DIAMOND_LEGGINGS,
    ITEM_DIAMOND_BOOTS,

    /* Food & Crops */
    ITEM_APPLE,
    ITEM_BREAD,
    ITEM_WHEAT,
    ITEM_COOKED_BEEF,

    /* Tools & Weapons */
    ITEM_WOODEN_PICKAXE,
    ITEM_STONE_PICKAXE,
    ITEM_IRON_PICKAXE,
    ITEM_DIAMOND_PICKAXE,
    ITEM_WOODEN_AXE,
    ITEM_STONE_AXE,
    ITEM_IRON_AXE,
    ITEM_DIAMOND_AXE,
    ITEM_WOODEN_SHOVEL,
    ITEM_STONE_SHOVEL,
    ITEM_IRON_SHOVEL,
    ITEM_DIAMOND_SHOVEL,
    ITEM_WOODEN_SWORD,
    ITEM_STONE_SWORD,
    ITEM_IRON_SWORD,
    ITEM_DIAMOND_SWORD,

    /* Deepslate Ores */
    BLOCK_DEEPSLATE_COAL_ORE,
    BLOCK_DEEPSLATE_IRON_ORE,
    BLOCK_DEEPSLATE_COPPER_ORE,
    BLOCK_DEEPSLATE_GOLD_ORE,
    BLOCK_DEEPSLATE_REDSTONE_ORE,
    BLOCK_DEEPSLATE_EMERALD_ORE,
    BLOCK_DEEPSLATE_LAPIS_ORE,
    BLOCK_DEEPSLATE_DIAMOND_ORE,

    /* Deepslate Building Blocks */
    BLOCK_DEEPSLATE_BRICKS,
    BLOCK_DEEPSLATE_TILES,
    BLOCK_POLISHED_DEEPSLATE,
    BLOCK_CHISELED_DEEPSLATE,

    /* Cherry Wood Set */
    BLOCK_CHERRY_LOG,
    BLOCK_CHERRY_PLANKS,
    BLOCK_CHERRY_LEAVES,
    BLOCK_CHERRY_SAPLING,

    /* Mud & Tuff Set */
    BLOCK_MUD,
    BLOCK_MUD_BRICKS,
    BLOCK_PACKED_MUD,
    BLOCK_TUFF_BRICKS,

    /* Badlands Terracotta & Geological Blocks */
    BLOCK_TERRACOTTA,
    BLOCK_RED_TERRACOTTA,
    BLOCK_ORANGE_TERRACOTTA,
    BLOCK_YELLOW_TERRACOTTA,
    BLOCK_BROWN_TERRACOTTA,
    BLOCK_WHITE_TERRACOTTA,
    BLOCK_LIGHT_GRAY_TERRACOTTA,
    BLOCK_BLUE_ICE,

    /* 26.3 Mechanics & Iconic Items */
    BLOCK_CRAFTER,
    BLOCK_HEAVY_CORE,
    ITEM_MACE,

    /* Pale Garden Set */
    BLOCK_PALE_OAK_LOG,
    BLOCK_PALE_OAK_PLANKS,
    BLOCK_PALE_OAK_LEAVES,
    BLOCK_PALE_MOSS_BLOCK,
    BLOCK_CREAKING_HEART,
    BLOCK_PALE_HANGING_MOSS,

    /* 26.3 Sulfur Caves Set */
    BLOCK_SULFUR,
    BLOCK_CINNABAR,
    BLOCK_POTENT_SULFUR,
    BLOCK_SULFUR_SPIKE,

    /* 26.3 Dripstone Caves Set */
    BLOCK_DRIPSTONE_BLOCK,
    BLOCK_POINTED_DRIPSTONE,

    /* 26.3 Deep Dark Sculk Set */
    BLOCK_SCULK,
    BLOCK_SCULK_CATALYST,
    BLOCK_SCULK_SENSOR,
    BLOCK_SCULK_SHRIEKER,

    /* 26.3 Lush Caves Flora */
    BLOCK_AZALEA_LEAVES,
    BLOCK_FLOWERING_AZALEA_LEAVES,

    BLOCK_COUNT
} BlockId;

#define ITEM_SUGAR_CANE BLOCK_SUGAR_CANE

typedef enum {
    TOOL_NONE = 0,
    TOOL_PICKAXE,
    TOOL_AXE,
    TOOL_SHOVEL,
    TOOL_SWORD
} ToolType;

typedef enum {
    FACE_TOP = 0,
    FACE_BOTTOM,
    FACE_NORTH,
    FACE_SOUTH,
    FACE_EAST,
    FACE_WEST,
    FACE_COUNT
} BlockFace;

typedef enum {
    TINT_NONE = 0,
    TINT_TOP,
    TINT_ALL
} TintMode;

typedef enum {
    SHAPE_CUBE = 0,
    SHAPE_SLAB,
    SHAPE_STAIRS,
    SHAPE_TORCH,
    SHAPE_CROSS,
    SHAPE_FLAT_FLOOR,
    SHAPE_DOOR,
    SHAPE_CACTUS
} BlockShape;

typedef struct {
    const char *name;
    const char *tex_top;
    const char *tex_bottom;
    const char *tex_side;
    const char *tex_front;
    bool solid;
    bool opaque;
    bool liquid;
    bool replaceable;
    bool is_item;
    BlockShape shape;
    TintMode tint_mode;
    float tint_r, tint_g, tint_b;
    uint8_t light;
} BlockDef;

extern const BlockDef g_block_defs[BLOCK_COUNT];
extern uint16_t g_block_tiles[BLOCK_COUNT][FACE_COUNT];

void blocks_init(const Assets *assets);
float block_hardness(uint8_t id);

ToolType item_tool_type(uint8_t id);
int item_tool_tier(uint8_t id);
ToolType block_required_tool(uint8_t block_id);
bool block_can_harvest(uint8_t block_id, uint8_t held_item);
float block_break_time(uint8_t block_id, uint8_t held_item);
uint8_t block_drop_item(uint8_t block_id, uint8_t held_item);

bool item_is_food(uint8_t id, float *out_food, float *out_saturation);
bool item_is_fuel(uint8_t id, float *out_burn_time);
uint8_t item_smelt_result(uint8_t id);
bool item_is_armor(uint8_t item);
int armor_slot_for_item(uint8_t item);
int armor_points_for_item(uint8_t item);

static inline bool block_is_air(uint8_t id) {
    return id == BLOCK_AIR;
}

static inline bool block_is_solid(uint8_t id) {
    return id < BLOCK_COUNT && g_block_defs[id].solid;
}

static inline bool block_is_opaque(uint8_t id) {
    return id < BLOCK_COUNT && g_block_defs[id].opaque;
}

static inline bool block_is_liquid(uint8_t id) {
    return id < BLOCK_COUNT && g_block_defs[id].liquid;
}

#endif

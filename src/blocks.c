#include "blocks.h"

#include "mced.h"

#define CUBE(ID, LABEL, TEX) \
    [ID] = {.name = LABEL, .tex_top = TEX, .tex_bottom = TEX, .tex_side = TEX, \
            .solid = true, .opaque = true}

#define CUBE_TB(ID, LABEL, TOP, BOTTOM, SIDE) \
    [ID] = {.name = LABEL, .tex_top = TOP, .tex_bottom = BOTTOM, .tex_side = SIDE, \
            .solid = true, .opaque = true}

#define CUBE_TINT(ID, LABEL, TEX, R, G, B) \
    [ID] = {.name = LABEL, .tex_top = TEX, .tex_bottom = TEX, .tex_side = TEX, \
            .solid = true, .opaque = true, .tint_mode = TINT_ALL, \
            .tint_r = R, .tint_g = G, .tint_b = B}

#define LEAVES(ID, LABEL, TEX, R, G, B) \
    [ID] = {.name = LABEL, .tex_top = TEX, .tex_bottom = TEX, .tex_side = TEX, \
            .solid = true, .opaque = false, .tint_mode = TINT_ALL, \
            .tint_r = R, .tint_g = G, .tint_b = B}

const BlockDef g_block_defs[BLOCK_COUNT] = {
    [BLOCK_AIR] = {.name = "Air", .replaceable = true},
    CUBE(BLOCK_STONE, "Stone", "block/stone"),
    CUBE(BLOCK_GRANITE, "Granite", "block/granite"),
    CUBE(BLOCK_DIORITE, "Diorite", "block/diorite"),
    CUBE(BLOCK_ANDESITE, "Andesite", "block/andesite"),
    CUBE_TB(BLOCK_DEEPSLATE, "Deepslate", "block/deepslate_top", "block/deepslate_top", "block/deepslate"),
    CUBE(BLOCK_COBBLESTONE, "Cobblestone", "block/cobblestone"),
    CUBE(BLOCK_COBBLED_DEEPSLATE, "Cobbled Deepslate", "block/cobbled_deepslate"),
    CUBE(BLOCK_DIRT, "Dirt", "block/dirt"),
    CUBE(BLOCK_COARSE_DIRT, "Coarse Dirt", "block/coarse_dirt"),
    [BLOCK_GRASS_BLOCK] = {
        .name = "Grass Block", .tex_top = "block/grass_block_top",
        .tex_bottom = "block/dirt", .tex_side = "block/grass_block_side",
        .solid = true, .opaque = true, .tint_mode = TINT_TOP,
        .tint_r = 0.50f, .tint_g = 0.82f, .tint_b = 0.30f},
    CUBE_TB(BLOCK_PODZOL, "Podzol", "block/podzol_top", "block/dirt", "block/podzol_side"),
    CUBE_TB(BLOCK_MYCELIUM, "Mycelium", "block/mycelium_top", "block/dirt", "block/mycelium_side"),
    CUBE(BLOCK_SAND, "Sand", "block/sand"),
    CUBE(BLOCK_RED_SAND, "Red Sand", "block/red_sand"),
    CUBE(BLOCK_GRAVEL, "Gravel", "block/gravel"),
    CUBE(BLOCK_CLAY, "Clay", "block/clay"),
    CUBE_TB(BLOCK_SANDSTONE, "Sandstone", "block/sandstone_top", "block/sandstone_bottom", "block/sandstone"),
    CUBE_TB(BLOCK_RED_SANDSTONE, "Red Sandstone", "block/red_sandstone_top", "block/red_sandstone_bottom", "block/red_sandstone"),
    CUBE(BLOCK_SNOW_BLOCK, "Snow Block", "block/snow"),
    [BLOCK_ICE] = {.name = "Ice", .tex_top = "block/ice", .tex_bottom = "block/ice",
                   .tex_side = "block/ice", .solid = true, .opaque = false},
    CUBE(BLOCK_PACKED_ICE, "Packed Ice", "block/packed_ice"),
    CUBE(BLOCK_BEDROCK, "Bedrock", "block/bedrock"),
    CUBE(BLOCK_COAL_ORE, "Coal Ore", "block/coal_ore"),
    CUBE(BLOCK_IRON_ORE, "Iron Ore", "block/iron_ore"),
    CUBE(BLOCK_COPPER_ORE, "Copper Ore", "block/copper_ore"),
    CUBE(BLOCK_GOLD_ORE, "Gold Ore", "block/gold_ore"),
    CUBE(BLOCK_REDSTONE_ORE, "Redstone Ore", "block/redstone_ore"),
    CUBE(BLOCK_LAPIS_ORE, "Lapis Ore", "block/lapis_ore"),
    CUBE(BLOCK_DIAMOND_ORE, "Diamond Ore", "block/diamond_ore"),
    CUBE(BLOCK_EMERALD_ORE, "Emerald Ore", "block/emerald_ore"),
    CUBE_TB(BLOCK_OAK_LOG, "Oak Log", "block/oak_log_top", "block/oak_log_top", "block/oak_log"),
    CUBE_TB(BLOCK_BIRCH_LOG, "Birch Log", "block/birch_log_top", "block/birch_log_top", "block/birch_log"),
    CUBE_TB(BLOCK_SPRUCE_LOG, "Spruce Log", "block/spruce_log_top", "block/spruce_log_top", "block/spruce_log"),
    CUBE_TB(BLOCK_JUNGLE_LOG, "Jungle Log", "block/jungle_log_top", "block/jungle_log_top", "block/jungle_log"),
    CUBE_TB(BLOCK_ACACIA_LOG, "Acacia Log", "block/acacia_log_top", "block/acacia_log_top", "block/acacia_log"),
    CUBE_TB(BLOCK_DARK_OAK_LOG, "Dark Oak Log", "block/dark_oak_log_top", "block/dark_oak_log_top", "block/dark_oak_log"),
    LEAVES(BLOCK_OAK_LEAVES, "Oak Leaves", "block/oak_leaves", 0.45f, 0.76f, 0.28f),
    LEAVES(BLOCK_BIRCH_LEAVES, "Birch Leaves", "block/birch_leaves", 0.55f, 0.79f, 0.35f),
    LEAVES(BLOCK_SPRUCE_LEAVES, "Spruce Leaves", "block/spruce_leaves", 0.34f, 0.58f, 0.32f),
    LEAVES(BLOCK_JUNGLE_LEAVES, "Jungle Leaves", "block/jungle_leaves", 0.38f, 0.78f, 0.24f),
    LEAVES(BLOCK_ACACIA_LEAVES, "Acacia Leaves", "block/acacia_leaves", 0.56f, 0.73f, 0.27f),
    LEAVES(BLOCK_DARK_OAK_LEAVES, "Dark Oak Leaves", "block/dark_oak_leaves", 0.33f, 0.57f, 0.22f),
    CUBE(BLOCK_OAK_PLANKS, "Oak Planks", "block/oak_planks"),
    CUBE(BLOCK_BIRCH_PLANKS, "Birch Planks", "block/birch_planks"),
    CUBE(BLOCK_SPRUCE_PLANKS, "Spruce Planks", "block/spruce_planks"),
    CUBE(BLOCK_JUNGLE_PLANKS, "Jungle Planks", "block/jungle_planks"),
    CUBE(BLOCK_ACACIA_PLANKS, "Acacia Planks", "block/acacia_planks"),
    CUBE(BLOCK_DARK_OAK_PLANKS, "Dark Oak Planks", "block/dark_oak_planks"),
    [BLOCK_GLASS] = {.name = "Glass", .tex_top = "block/glass", .tex_bottom = "block/glass",
                     .tex_side = "block/glass", .solid = true, .opaque = false},
    CUBE(BLOCK_BRICKS, "Bricks", "block/bricks"),
    CUBE(BLOCK_STONE_BRICKS, "Stone Bricks", "block/stone_bricks"),
    CUBE(BLOCK_MOSSY_COBBLESTONE, "Mossy Cobblestone", "block/mossy_cobblestone"),
    CUBE(BLOCK_OBSIDIAN, "Obsidian", "block/obsidian"),
    CUBE(BLOCK_NETHERRACK, "Netherrack", "block/netherrack"),
    CUBE(BLOCK_SOUL_SAND, "Soul Sand", "block/soul_sand"),
    [BLOCK_GLOWSTONE] = {.name = "Glowstone", .tex_top = "block/glowstone", .tex_bottom = "block/glowstone",
                         .tex_side = "block/glowstone", .solid = true, .opaque = true, .light = 15},
    CUBE(BLOCK_SEA_LANTERN, "Sea Lantern", "block/sea_lantern"),
    [BLOCK_CRAFTING_TABLE] = {
        .name = "Crafting Table", .tex_top = "block/crafting_table_top",
        .tex_bottom = "block/oak_planks", .tex_side = "block/crafting_table_side",
        .tex_front = "block/crafting_table_front", .solid = true, .opaque = true},
    [BLOCK_FURNACE] = {
        .name = "Furnace", .tex_top = "block/furnace_top", .tex_bottom = "block/furnace_top",
        .tex_side = "block/furnace_side", .tex_front = "block/furnace_front",
        .solid = true, .opaque = true},
    [BLOCK_CHEST] = {
        .name = "Chest", .tex_top = "block/oak_planks", .tex_bottom = "block/oak_planks",
        .tex_side = "block/oak_planks", .tex_front = "block/oak_planks",
        .solid = true, .opaque = false},
    CUBE_TB(BLOCK_BOOKSHELF, "Bookshelf", "block/oak_planks", "block/oak_planks", "block/bookshelf"),
    CUBE(BLOCK_WHITE_WOOL, "White Wool", "block/white_wool"),
    CUBE(BLOCK_RED_WOOL, "Red Wool", "block/red_wool"),
    CUBE(BLOCK_ORANGE_WOOL, "Orange Wool", "block/orange_wool"),
    CUBE(BLOCK_YELLOW_WOOL, "Yellow Wool", "block/yellow_wool"),
    CUBE(BLOCK_LIME_WOOL, "Lime Wool", "block/lime_wool"),
    CUBE(BLOCK_BLUE_WOOL, "Blue Wool", "block/blue_wool"),
    CUBE(BLOCK_PURPLE_WOOL, "Purple Wool", "block/purple_wool"),
    CUBE(BLOCK_BLACK_WOOL, "Black Wool", "block/black_wool"),
    [BLOCK_CACTUS] = {
        .name = "Cactus", .tex_top = "block/cactus_top", .tex_bottom = "block/cactus_bottom",
        .tex_side = "block/cactus_side", .solid = true, .opaque = false, .shape = SHAPE_CACTUS},
    CUBE_TB(BLOCK_PUMPKIN, "Pumpkin", "block/pumpkin_top", "block/pumpkin_top", "block/pumpkin_side"),
    [BLOCK_CARVED_PUMPKIN] = {
        .name = "Carved Pumpkin", .tex_top = "block/pumpkin_top", .tex_bottom = "block/pumpkin_top",
        .tex_side = "block/pumpkin_side", .tex_front = "block/carved_pumpkin",
        .solid = true, .opaque = true},
    [BLOCK_JACK_O_LANTERN] = {
        .name = "Jack o'Lantern", .tex_top = "block/pumpkin_top", .tex_bottom = "block/pumpkin_top",
        .tex_side = "block/pumpkin_side", .tex_front = "block/jack_o_lantern",
        .solid = true, .opaque = true, .light = 15},
    CUBE_TB(BLOCK_MELON, "Melon", "block/melon_top", "block/melon_top", "block/melon_side"),
    CUBE_TB(BLOCK_HAY_BLOCK, "Hay Block", "block/hay_block_top", "block/hay_block_top", "block/hay_block_side"),
    CUBE(BLOCK_SPONGE, "Sponge", "block/sponge"),
    CUBE(BLOCK_SLIME_BLOCK, "Slime Block", "block/slime_block"),
    CUBE_TB(BLOCK_HONEY_BLOCK, "Honey Block", "block/honey_block_top", "block/honey_block_bottom", "block/honey_block_side"),
    CUBE(BLOCK_MOSS_BLOCK, "Moss Block", "block/moss_block"),
    CUBE(BLOCK_CALCITE, "Calcite", "block/calcite"),
    CUBE(BLOCK_TUFF, "Tuff", "block/tuff"),
    CUBE(BLOCK_AMETHYST_BLOCK, "Amethyst Block", "block/amethyst_block"),
    CUBE(BLOCK_COPPER_BLOCK, "Block of Copper", "block/copper_block"),
    CUBE(BLOCK_IRON_BLOCK, "Block of Iron", "block/iron_block"),
    CUBE(BLOCK_GOLD_BLOCK, "Block of Gold", "block/gold_block"),
    CUBE(BLOCK_DIAMOND_BLOCK, "Block of Diamond", "block/diamond_block"),
    CUBE(BLOCK_EMERALD_BLOCK, "Block of Emerald", "block/emerald_block"),
    CUBE(BLOCK_LAPIS_BLOCK, "Block of Lapis", "block/lapis_block"),
    CUBE(BLOCK_REDSTONE_BLOCK, "Block of Redstone", "block/redstone_block"),
    CUBE(BLOCK_COAL_BLOCK, "Block of Coal", "block/coal_block"),
    CUBE_TB(BLOCK_QUARTZ_BLOCK, "Quartz Block", "block/quartz_block_top", "block/quartz_block_bottom", "block/quartz_block_side"),
    CUBE(BLOCK_PURPUR_BLOCK, "Purpur Block", "block/purpur_block"),
    CUBE(BLOCK_END_STONE, "End Stone", "block/end_stone"),
    CUBE(BLOCK_PRISMARINE, "Prismarine", "block/prismarine"),
    CUBE(BLOCK_DARK_PRISMARINE, "Dark Prismarine", "block/dark_prismarine"),
    CUBE(BLOCK_MUSHROOM_STEM, "Mushroom Stem", "block/mushroom_stem"),
    CUBE(BLOCK_RED_MUSHROOM_BLOCK, "Red Mushroom Block", "block/red_mushroom_block"),
    CUBE(BLOCK_BROWN_MUSHROOM_BLOCK, "Brown Mushroom Block", "block/brown_mushroom_block"),
    [BLOCK_WATER] = {
        .name = "Water", .tex_top = "block/water_still", .tex_bottom = "block/water_still",
        .tex_side = "block/water_still", .solid = false, .opaque = false, .liquid = true,
        .replaceable = true, .tint_mode = TINT_ALL,
        .tint_r = 0.24f, .tint_g = 0.43f, .tint_b = 0.85f},
    [BLOCK_LAVA] = {
        .name = "Lava", .tex_top = "block/lava_still", .tex_bottom = "block/lava_still",
        .tex_side = "block/lava_still", .solid = false, .opaque = false, .liquid = true,
        .replaceable = true, .tint_mode = TINT_ALL,
        .tint_r = 1.00f, .tint_g = 0.55f, .tint_b = 0.15f, .light = 15},
    [BLOCK_TNT] = {
        .name = "TNT", .tex_top = "block/tnt_top", .tex_bottom = "block/tnt_bottom",
        .tex_side = "block/tnt_side", .solid = true, .opaque = true},
    [BLOCK_TORCH] = {
        .name = "Torch", .tex_top = "block/torch", .tex_bottom = "block/torch",
        .tex_side = "block/torch", .solid = false, .opaque = false, .shape = SHAPE_TORCH, .light = 14},

    [BLOCK_OAK_SLAB] = {
        .name = "Oak Slab", .tex_top = "block/oak_planks", .tex_bottom = "block/oak_planks",
        .tex_side = "block/oak_planks", .solid = true, .opaque = false, .shape = SHAPE_SLAB},
    [BLOCK_COBBLESTONE_SLAB] = {
        .name = "Cobblestone Slab", .tex_top = "block/cobblestone", .tex_bottom = "block/cobblestone",
        .tex_side = "block/cobblestone", .solid = true, .opaque = false, .shape = SHAPE_SLAB},
    [BLOCK_STONE_SLAB] = {
        .name = "Stone Slab", .tex_top = "block/stone", .tex_bottom = "block/stone",
        .tex_side = "block/stone", .solid = true, .opaque = false, .shape = SHAPE_SLAB},
    [BLOCK_STONE_BRICK_SLAB] = {
        .name = "Stone Brick Slab", .tex_top = "block/stone_bricks", .tex_bottom = "block/stone_bricks",
        .tex_side = "block/stone_bricks", .solid = true, .opaque = false, .shape = SHAPE_SLAB},

    [BLOCK_OAK_STAIRS] = {
        .name = "Oak Stairs", .tex_top = "block/oak_planks", .tex_bottom = "block/oak_planks",
        .tex_side = "block/oak_planks", .solid = true, .opaque = false, .shape = SHAPE_STAIRS},
    [BLOCK_COBBLESTONE_STAIRS] = {
        .name = "Cobblestone Stairs", .tex_top = "block/cobblestone", .tex_bottom = "block/cobblestone",
        .tex_side = "block/cobblestone", .solid = true, .opaque = false, .shape = SHAPE_STAIRS},
    [BLOCK_STONE_BRICK_STAIRS] = {
        .name = "Stone Brick Stairs", .tex_top = "block/stone_bricks", .tex_bottom = "block/stone_bricks",
        .tex_side = "block/stone_bricks", .solid = true, .opaque = false, .shape = SHAPE_STAIRS},

    [BLOCK_REDSTONE_WIRE] = {
        .name = "Redstone Dust", .tex_top = "block/redstone_dust_dot", .tex_bottom = "block/redstone_dust_dot",
        .tex_side = "block/redstone_dust_dot", .solid = false, .opaque = false, .shape = SHAPE_FLAT_FLOOR,
        .replaceable = true, .tint_mode = TINT_ALL, .tint_r = 0.50f, .tint_g = 0.05f, .tint_b = 0.05f},
    [BLOCK_REDSTONE_TORCH] = {
        .name = "Redstone Torch", .tex_top = "block/redstone_torch", .tex_bottom = "block/redstone_torch",
        .tex_side = "block/redstone_torch", .solid = false, .opaque = false, .shape = SHAPE_TORCH, .light = 7},
    [BLOCK_LEVER] = {
        .name = "Lever", .tex_top = "block/lever", .tex_bottom = "block/lever",
        .tex_side = "block/lever", .solid = false, .opaque = false, .shape = SHAPE_TORCH},
    [BLOCK_STONE_BUTTON] = {
        .name = "Stone Button", .tex_top = "block/stone", .tex_bottom = "block/stone",
        .tex_side = "block/stone", .solid = false, .opaque = false, .shape = SHAPE_TORCH},
    [BLOCK_REDSTONE_LAMP_OFF] = {
        .name = "Redstone Lamp", .tex_top = "block/redstone_lamp", .tex_bottom = "block/redstone_lamp",
        .tex_side = "block/redstone_lamp", .solid = true, .opaque = true},
    [BLOCK_REDSTONE_LAMP_ON] = {
        .name = "Redstone Lamp (Lit)", .tex_top = "block/redstone_lamp_on", .tex_bottom = "block/redstone_lamp_on",
        .tex_side = "block/redstone_lamp_on", .solid = true, .opaque = true, .light = 15},
    [BLOCK_OAK_DOOR] = {
        .name = "Oak Door", .tex_top = "item/oak_door", .tex_bottom = "block/oak_door_bottom",
        .tex_side = "block/oak_door_bottom", .solid = true, .opaque = false, .shape = SHAPE_DOOR},
    [BLOCK_OAK_DOOR_OPEN] = {
        .name = "Oak Door", .tex_top = "item/oak_door", .tex_bottom = "block/oak_door_bottom",
        .tex_side = "block/oak_door_bottom", .solid = false, .opaque = false, .shape = SHAPE_DOOR},
    [BLOCK_IRON_DOOR] = {
        .name = "Iron Door", .tex_top = "item/iron_door", .tex_bottom = "block/iron_door_bottom",
        .tex_side = "block/iron_door_bottom", .solid = true, .opaque = false, .shape = SHAPE_DOOR},
    [BLOCK_IRON_DOOR_OPEN] = {
        .name = "Iron Door", .tex_top = "item/iron_door", .tex_bottom = "block/iron_door_bottom",
        .tex_side = "block/iron_door_bottom", .solid = false, .opaque = false, .shape = SHAPE_DOOR},
    [BLOCK_OAK_TRAPDOOR] = {
        .name = "Oak Trapdoor", .tex_top = "block/oak_trapdoor", .tex_bottom = "block/oak_trapdoor",
        .tex_side = "block/oak_trapdoor", .solid = true, .opaque = false, .shape = SHAPE_SLAB},
    [BLOCK_OAK_TRAPDOOR_OPEN] = {
        .name = "Oak Trapdoor", .tex_top = "block/oak_trapdoor", .tex_bottom = "block/oak_trapdoor",
        .tex_side = "block/oak_trapdoor", .solid = false, .opaque = false, .shape = SHAPE_SLAB},

    [BLOCK_DANDELION] = {
        .name = "Dandelion", .tex_top = "block/dandelion", .tex_bottom = "block/dandelion",
        .tex_side = "block/dandelion", .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_POPPY] = {
        .name = "Poppy", .tex_top = "block/poppy", .tex_bottom = "block/poppy",
        .tex_side = "block/poppy", .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_SHORT_GRASS] = {
        .name = "Grass", .tex_top = "block/short_grass", .tex_bottom = "block/short_grass",
        .tex_side = "block/short_grass", .solid = false, .opaque = false, .shape = SHAPE_CROSS,
        .tint_mode = TINT_ALL, .tint_r = 0.48f, .tint_g = 0.78f, .tint_b = 0.28f},
    [BLOCK_SUGAR_CANE] = {
        .name = "Sugar Cane", .tex_top = "block/sugar_cane", .tex_bottom = "block/sugar_cane",
        .tex_side = "block/sugar_cane", .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_DEAD_BUSH] = {
        .name = "Dead Bush", .tex_top = "block/dead_bush", .tex_bottom = "block/dead_bush",
        .tex_side = "block/dead_bush", .solid = false, .opaque = false, .shape = SHAPE_CROSS},

    /* Crafting & Resource Items */
    [ITEM_STICK] = {.name = "Stick", .tex_top = "item/stick", .tex_bottom = "item/stick", .tex_side = "item/stick", .is_item = true},
    [ITEM_COAL] = {.name = "Coal", .tex_top = "item/coal", .tex_bottom = "item/coal", .tex_side = "item/coal", .is_item = true},
    [ITEM_CHARCOAL] = {.name = "Charcoal", .tex_top = "item/charcoal", .tex_bottom = "item/charcoal", .tex_side = "item/charcoal", .is_item = true},
    [ITEM_RAW_IRON] = {.name = "Raw Iron", .tex_top = "item/raw_iron", .tex_bottom = "item/raw_iron", .tex_side = "item/raw_iron", .is_item = true},
    [ITEM_IRON_INGOT] = {.name = "Iron Ingot", .tex_top = "item/iron_ingot", .tex_bottom = "item/iron_ingot", .tex_side = "item/iron_ingot", .is_item = true},
    [ITEM_RAW_GOLD] = {.name = "Raw Gold", .tex_top = "item/raw_gold", .tex_bottom = "item/raw_gold", .tex_side = "item/raw_gold", .is_item = true},
    [ITEM_GOLD_INGOT] = {.name = "Gold Ingot", .tex_top = "item/gold_ingot", .tex_bottom = "item/gold_ingot", .tex_side = "item/gold_ingot", .is_item = true},
    [ITEM_DIAMOND] = {.name = "Diamond", .tex_top = "item/diamond", .tex_bottom = "item/diamond", .tex_side = "item/diamond", .is_item = true},
    [ITEM_EMERALD] = {.name = "Emerald", .tex_top = "item/emerald", .tex_bottom = "item/emerald", .tex_side = "item/emerald", .is_item = true},
    [ITEM_RAW_COPPER] = {.name = "Raw Copper", .tex_top = "item/raw_copper", .tex_bottom = "item/raw_copper", .tex_side = "item/raw_copper", .is_item = true},
    [ITEM_LAPIS_LAZULI] = {.name = "Lapis Lazuli", .tex_top = "item/lapis_lazuli", .tex_bottom = "item/lapis_lazuli", .tex_side = "item/lapis_lazuli", .is_item = true},
    [ITEM_REDSTONE] = {.name = "Redstone Dust", .tex_top = "item/redstone", .tex_bottom = "item/redstone", .tex_side = "item/redstone", .is_item = true},
    [ITEM_BONE] = {.name = "Bone", .tex_top = "item/bone", .tex_bottom = "item/bone", .tex_side = "item/bone", .is_item = true},
    [ITEM_ARROW] = {.name = "Arrow", .tex_top = "item/arrow", .tex_bottom = "item/arrow", .tex_side = "item/arrow", .is_item = true},
    [ITEM_BOW] = {.name = "Bow", .tex_top = "item/bow", .tex_bottom = "item/bow", .tex_side = "item/bow", .is_item = true},
    [ITEM_ROTTEN_FLESH] = {.name = "Rotten Flesh", .tex_top = "item/rotten_flesh", .tex_bottom = "item/rotten_flesh", .tex_side = "item/rotten_flesh", .is_item = true},
    [ITEM_BUCKET] = {.name = "Bucket", .tex_top = "item/bucket", .tex_bottom = "item/bucket", .tex_side = "item/bucket", .is_item = true},
    [ITEM_WATER_BUCKET] = {.name = "Water Bucket", .tex_top = "item/water_bucket", .tex_bottom = "item/water_bucket", .tex_side = "item/water_bucket", .is_item = true},
    [ITEM_LAVA_BUCKET] = {.name = "Lava Bucket", .tex_top = "item/lava_bucket", .tex_bottom = "item/lava_bucket", .tex_side = "item/lava_bucket", .is_item = true},
    [ITEM_MILK_BUCKET] = {.name = "Milk Bucket", .tex_top = "item/milk_bucket", .tex_bottom = "item/milk_bucket", .tex_side = "item/milk_bucket", .is_item = true},
    [ITEM_WHEAT_SEEDS] = {.name = "Wheat Seeds", .tex_top = "item/wheat_seeds", .tex_bottom = "item/wheat_seeds", .tex_side = "item/wheat_seeds", .is_item = true},
    [ITEM_SUGAR] = {.name = "Sugar", .tex_top = "item/sugar", .tex_bottom = "item/sugar", .tex_side = "item/sugar", .is_item = true},
    [ITEM_PAPER] = {.name = "Paper", .tex_top = "item/paper", .tex_bottom = "item/paper", .tex_side = "item/paper", .is_item = true},

    /* Armor */
    [ITEM_IRON_HELMET] = {.name = "Iron Helmet", .tex_top = "item/iron_helmet", .tex_bottom = "item/iron_helmet", .tex_side = "item/iron_helmet", .is_item = true},
    [ITEM_IRON_CHESTPLATE] = {.name = "Iron Chestplate", .tex_top = "item/iron_chestplate", .tex_bottom = "item/iron_chestplate", .tex_side = "item/iron_chestplate", .is_item = true},
    [ITEM_IRON_LEGGINGS] = {.name = "Iron Leggings", .tex_top = "item/iron_leggings", .tex_bottom = "item/iron_leggings", .tex_side = "item/iron_leggings", .is_item = true},
    [ITEM_IRON_BOOTS] = {.name = "Iron Boots", .tex_top = "item/iron_boots", .tex_bottom = "item/iron_boots", .tex_side = "item/iron_boots", .is_item = true},

    [ITEM_DIAMOND_HELMET] = {.name = "Diamond Helmet", .tex_top = "item/diamond_helmet", .tex_bottom = "item/diamond_helmet", .tex_side = "item/diamond_helmet", .is_item = true},
    [ITEM_DIAMOND_CHESTPLATE] = {.name = "Diamond Chestplate", .tex_top = "item/diamond_chestplate", .tex_bottom = "item/diamond_chestplate", .tex_side = "item/diamond_chestplate", .is_item = true},
    [ITEM_DIAMOND_LEGGINGS] = {.name = "Diamond Leggings", .tex_top = "item/diamond_leggings", .tex_bottom = "item/diamond_leggings", .tex_side = "item/diamond_leggings", .is_item = true},
    [ITEM_DIAMOND_BOOTS] = {.name = "Diamond Boots", .tex_top = "item/diamond_boots", .tex_bottom = "item/diamond_boots", .tex_side = "item/diamond_boots", .is_item = true},

    /* Food & Crops */
    [ITEM_APPLE] = {.name = "Apple", .tex_top = "item/apple", .tex_bottom = "item/apple", .tex_side = "item/apple", .is_item = true},
    [ITEM_BREAD] = {.name = "Bread", .tex_top = "item/bread", .tex_bottom = "item/bread", .tex_side = "item/bread", .is_item = true},
    [ITEM_WHEAT] = {.name = "Wheat", .tex_top = "item/wheat", .tex_bottom = "item/wheat", .tex_side = "item/wheat", .is_item = true},
    [ITEM_COOKED_BEEF] = {.name = "Steak", .tex_top = "item/cooked_beef", .tex_bottom = "item/cooked_beef", .tex_side = "item/cooked_beef", .is_item = true},

    /* Tools & Weapons */
    [ITEM_WOODEN_PICKAXE] = {.name = "Wooden Pickaxe", .tex_top = "item/wooden_pickaxe", .tex_bottom = "item/wooden_pickaxe", .tex_side = "item/wooden_pickaxe", .is_item = true},
    [ITEM_STONE_PICKAXE] = {.name = "Stone Pickaxe", .tex_top = "item/stone_pickaxe", .tex_bottom = "item/stone_pickaxe", .tex_side = "item/stone_pickaxe", .is_item = true},
    [ITEM_IRON_PICKAXE] = {.name = "Iron Pickaxe", .tex_top = "item/iron_pickaxe", .tex_bottom = "item/iron_pickaxe", .tex_side = "item/iron_pickaxe", .is_item = true},
    [ITEM_DIAMOND_PICKAXE] = {.name = "Diamond Pickaxe", .tex_top = "item/diamond_pickaxe", .tex_bottom = "item/diamond_pickaxe", .tex_side = "item/diamond_pickaxe", .is_item = true},

    [ITEM_WOODEN_AXE] = {.name = "Wooden Axe", .tex_top = "item/wooden_axe", .tex_bottom = "item/wooden_axe", .tex_side = "item/wooden_axe", .is_item = true},
    [ITEM_STONE_AXE] = {.name = "Stone Axe", .tex_top = "item/stone_axe", .tex_bottom = "item/stone_axe", .tex_side = "item/stone_axe", .is_item = true},
    [ITEM_IRON_AXE] = {.name = "Iron Axe", .tex_top = "item/iron_axe", .tex_bottom = "item/iron_axe", .tex_side = "item/iron_axe", .is_item = true},
    [ITEM_DIAMOND_AXE] = {.name = "Diamond Axe", .tex_top = "item/diamond_axe", .tex_bottom = "item/diamond_axe", .tex_side = "item/diamond_axe", .is_item = true},

    [ITEM_WOODEN_SHOVEL] = {.name = "Wooden Shovel", .tex_top = "item/wooden_shovel", .tex_bottom = "item/wooden_shovel", .tex_side = "item/wooden_shovel", .is_item = true},
    [ITEM_STONE_SHOVEL] = {.name = "Stone Shovel", .tex_top = "item/stone_shovel", .tex_bottom = "item/stone_shovel", .tex_side = "item/stone_shovel", .is_item = true},
    [ITEM_IRON_SHOVEL] = {.name = "Iron Shovel", .tex_top = "item/iron_shovel", .tex_bottom = "item/iron_shovel", .tex_side = "item/iron_shovel", .is_item = true},
    [ITEM_DIAMOND_SHOVEL] = {.name = "Diamond Shovel", .tex_top = "item/diamond_shovel", .tex_bottom = "item/diamond_shovel", .tex_side = "item/diamond_shovel", .is_item = true},

    [ITEM_WOODEN_SWORD] = {.name = "Wooden Sword", .tex_top = "item/wooden_sword", .tex_bottom = "item/wooden_sword", .tex_side = "item/wooden_sword", .is_item = true},
    [ITEM_STONE_SWORD] = {.name = "Stone Sword", .tex_top = "item/stone_sword", .tex_bottom = "item/stone_sword", .tex_side = "item/stone_sword", .is_item = true},
    [ITEM_IRON_SWORD] = {.name = "Iron Sword", .tex_top = "item/iron_sword", .tex_bottom = "item/iron_sword", .tex_side = "item/iron_sword", .is_item = true},
    [ITEM_DIAMOND_SWORD] = {.name = "Diamond Sword", .tex_top = "item/diamond_sword", .tex_bottom = "item/diamond_sword", .tex_side = "item/diamond_sword", .is_item = true},

    /* Deepslate Ores */
    CUBE(BLOCK_DEEPSLATE_COAL_ORE, "Deepslate Coal Ore", "block/deepslate_coal_ore"),
    CUBE(BLOCK_DEEPSLATE_IRON_ORE, "Deepslate Iron Ore", "block/deepslate_iron_ore"),
    CUBE(BLOCK_DEEPSLATE_COPPER_ORE, "Deepslate Copper Ore", "block/deepslate_copper_ore"),
    CUBE(BLOCK_DEEPSLATE_GOLD_ORE, "Deepslate Gold Ore", "block/deepslate_gold_ore"),
    CUBE(BLOCK_DEEPSLATE_REDSTONE_ORE, "Deepslate Redstone Ore", "block/deepslate_redstone_ore"),
    CUBE(BLOCK_DEEPSLATE_EMERALD_ORE, "Deepslate Emerald Ore", "block/deepslate_emerald_ore"),
    CUBE(BLOCK_DEEPSLATE_LAPIS_ORE, "Deepslate Lapis Ore", "block/deepslate_lapis_ore"),
    CUBE(BLOCK_DEEPSLATE_DIAMOND_ORE, "Deepslate Diamond Ore", "block/deepslate_diamond_ore"),

    /* Deepslate Building Blocks */
    CUBE(BLOCK_DEEPSLATE_BRICKS, "Deepslate Bricks", "block/deepslate_bricks"),
    CUBE(BLOCK_DEEPSLATE_TILES, "Deepslate Tiles", "block/deepslate_tiles"),
    CUBE(BLOCK_POLISHED_DEEPSLATE, "Polished Deepslate", "block/polished_deepslate"),
    CUBE(BLOCK_CHISELED_DEEPSLATE, "Chiseled Deepslate", "block/chiseled_deepslate"),

    /* Cherry Wood Set */
    CUBE_TB(BLOCK_CHERRY_LOG, "Cherry Log", "block/cherry_log_top", "block/cherry_log_top", "block/cherry_log"),
    CUBE(BLOCK_CHERRY_PLANKS, "Cherry Planks", "block/cherry_planks"),
    LEAVES(BLOCK_CHERRY_LEAVES, "Cherry Leaves", "block/cherry_leaves", 1.0f, 1.0f, 1.0f),
    [BLOCK_CHERRY_SAPLING] = {
        .name = "Cherry Sapling", .tex_top = "block/cherry_sapling", .tex_bottom = "block/cherry_sapling",
        .tex_side = "block/cherry_sapling", .solid = false, .opaque = false, .shape = SHAPE_CROSS},

    /* Mud & Tuff Set */
    CUBE(BLOCK_MUD, "Mud", "block/mud"),
    CUBE(BLOCK_MUD_BRICKS, "Mud Bricks", "block/mud_bricks"),
    CUBE(BLOCK_PACKED_MUD, "Packed Mud", "block/packed_mud"),
    CUBE(BLOCK_TUFF_BRICKS, "Tuff Bricks", "block/tuff_bricks"),

    /* Badlands Terracotta & Geological Blocks */
    CUBE(BLOCK_TERRACOTTA, "Terracotta", "block/terracotta"),
    CUBE(BLOCK_RED_TERRACOTTA, "Red Terracotta", "block/red_terracotta"),
    CUBE(BLOCK_ORANGE_TERRACOTTA, "Orange Terracotta", "block/orange_terracotta"),
    CUBE(BLOCK_YELLOW_TERRACOTTA, "Yellow Terracotta", "block/yellow_terracotta"),
    CUBE(BLOCK_BROWN_TERRACOTTA, "Brown Terracotta", "block/brown_terracotta"),
    CUBE(BLOCK_WHITE_TERRACOTTA, "White Terracotta", "block/white_terracotta"),
    CUBE(BLOCK_LIGHT_GRAY_TERRACOTTA, "Light Gray Terracotta", "block/light_gray_terracotta"),
    CUBE(BLOCK_BLUE_ICE, "Blue Ice", "block/blue_ice"),

    /* 26.3 Mechanics & Iconic Items */
    [BLOCK_CRAFTER] = {.name = "Crafter", .tex_top = "block/crafter_top", .tex_bottom = "block/crafter_bottom", .tex_side = "block/crafter_east", .tex_front = "block/crafter_north", .solid = true, .opaque = true},
    CUBE(BLOCK_HEAVY_CORE, "Heavy Core", "block/heavy_core"),
    [ITEM_MACE] = {.name = "Mace", .tex_top = "item/mace", .tex_bottom = "item/mace", .tex_side = "item/mace", .is_item = true},

    /* Pale Garden Set */
    CUBE_TB(BLOCK_PALE_OAK_LOG, "Pale Oak Log", "block/pale_oak_log_top", "block/pale_oak_log_top", "block/pale_oak_log"),
    CUBE(BLOCK_PALE_OAK_PLANKS, "Pale Oak Planks", "block/pale_oak_planks"),
    LEAVES(BLOCK_PALE_OAK_LEAVES, "Pale Oak Leaves", "block/pale_oak_leaves", 1.0f, 1.0f, 1.0f),
    CUBE(BLOCK_PALE_MOSS_BLOCK, "Pale Moss Block", "block/pale_moss_block"),
    CUBE_TB(BLOCK_CREAKING_HEART, "Creaking Heart", "block/creaking_heart_top", "block/creaking_heart_top", "block/creaking_heart"),
    [BLOCK_PALE_HANGING_MOSS] = {.name = "Pale Hanging Moss", .tex_top = "block/pale_hanging_moss",
        .tex_bottom = "block/pale_hanging_moss",
        .tex_side = "block/pale_hanging_moss", .solid = false, .opaque = false, .shape = SHAPE_CROSS},

    /* 26.3 Sulfur Caves Set */
    CUBE(BLOCK_SULFUR, "Sulfur", "block/sulfur"),
    CUBE(BLOCK_CINNABAR, "Cinnabar", "block/cinnabar"),
    CUBE(BLOCK_POTENT_SULFUR, "Potent Sulfur", "block/potent_sulfur"),
    [BLOCK_SULFUR_SPIKE] = {.name = "Sulfur Spike", .tex_top = "block/sulfur_spike_up_tip",
        .tex_bottom = "block/sulfur_spike_up_tip",
        .tex_side = "block/sulfur_spike_up_tip", .solid = false, .opaque = false, .shape = SHAPE_CROSS},

    /* 26.3 Dripstone Caves Set */
    CUBE(BLOCK_DRIPSTONE_BLOCK, "Dripstone Block", "block/dripstone_block"),
    [BLOCK_POINTED_DRIPSTONE] = {.name = "Pointed Dripstone", .tex_top = "block/pointed_dripstone_up_tip",
        .tex_bottom = "block/pointed_dripstone_up_tip",
        .tex_side = "block/pointed_dripstone_up_tip", .solid = false, .opaque = false, .shape = SHAPE_CROSS},

    /* 26.3 Deep Dark Sculk Set */
    CUBE(BLOCK_SCULK, "Sculk", "block/sculk"),
    CUBE_TB(BLOCK_SCULK_CATALYST, "Sculk Catalyst", "block/sculk_catalyst_top", "block/sculk_catalyst_bottom", "block/sculk_catalyst_side"),
    CUBE_TB(BLOCK_SCULK_SENSOR, "Sculk Sensor", "block/sculk_sensor_top", "block/sculk_sensor_bottom", "block/sculk_sensor_side"),
    CUBE_TB(BLOCK_SCULK_SHRIEKER, "Sculk Shrieker", "block/sculk_shrieker_top", "block/sculk_shrieker_bottom", "block/sculk_shrieker_side"),

    /* 26.3 Lush Caves Flora */
    LEAVES(BLOCK_AZALEA_LEAVES, "Azalea Leaves", "block/azalea_leaves", 1.0f, 1.0f, 1.0f),
    LEAVES(BLOCK_FLOWERING_AZALEA_LEAVES, "Flowering Azalea Leaves", "block/flowering_azalea_leaves", 1.0f, 1.0f, 1.0f),

    /* Underwater Flora & Corals */
    [BLOCK_SEAGRASS] = {.name = "Seagrass", .tex_top = "block/seagrass", .tex_bottom = "block/seagrass", .tex_side = "block/seagrass",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS, .tint_mode = TINT_ALL, .tint_r = 0.40f, .tint_g = 0.82f, .tint_b = 0.35f},
    [BLOCK_KELP] = {.name = "Kelp", .tex_top = "block/kelp", .tex_bottom = "block/kelp", .tex_side = "block/kelp",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    CUBE(BLOCK_TUBE_CORAL_BLOCK, "Tube Coral Block", "block/tube_coral_block"),
    CUBE(BLOCK_BRAIN_CORAL_BLOCK, "Brain Coral Block", "block/brain_coral_block"),
    CUBE(BLOCK_BUBBLE_CORAL_BLOCK, "Bubble Coral Block", "block/bubble_coral_block"),
    CUBE(BLOCK_FIRE_CORAL_BLOCK, "Fire Coral Block", "block/fire_coral_block"),
    CUBE(BLOCK_HORN_CORAL_BLOCK, "Horn Coral Block", "block/horn_coral_block"),
    [BLOCK_TUBE_CORAL] = {.name = "Tube Coral", .tex_top = "block/tube_coral", .tex_bottom = "block/tube_coral", .tex_side = "block/tube_coral",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_BRAIN_CORAL] = {.name = "Brain Coral", .tex_top = "block/brain_coral", .tex_bottom = "block/brain_coral", .tex_side = "block/brain_coral",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_BUBBLE_CORAL] = {.name = "Bubble Coral", .tex_top = "block/bubble_coral", .tex_bottom = "block/bubble_coral", .tex_side = "block/bubble_coral",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_FIRE_CORAL] = {.name = "Fire Coral", .tex_top = "block/fire_coral", .tex_bottom = "block/fire_coral", .tex_side = "block/fire_coral",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_HORN_CORAL] = {.name = "Horn Coral", .tex_top = "block/horn_coral", .tex_bottom = "block/horn_coral", .tex_side = "block/horn_coral",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},

    /* Wildflowers & Lush Flora */
    [BLOCK_CORNFLOWER] = {.name = "Cornflower", .tex_top = "block/cornflower", .tex_bottom = "block/cornflower", .tex_side = "block/cornflower",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_ALLIUM] = {.name = "Allium", .tex_top = "block/allium", .tex_bottom = "block/allium", .tex_side = "block/allium",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_AZURE_BLUET] = {.name = "Azure Bluet", .tex_top = "block/azure_bluet", .tex_bottom = "block/azure_bluet", .tex_side = "block/azure_bluet",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_RED_TULIP] = {.name = "Red Tulip", .tex_top = "block/red_tulip", .tex_bottom = "block/red_tulip", .tex_side = "block/red_tulip",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_ORANGE_TULIP] = {.name = "Orange Tulip", .tex_top = "block/orange_tulip", .tex_bottom = "block/orange_tulip", .tex_side = "block/orange_tulip",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_WHITE_TULIP] = {.name = "White Tulip", .tex_top = "block/white_tulip", .tex_bottom = "block/white_tulip", .tex_side = "block/white_tulip",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_PINK_TULIP] = {.name = "Pink Tulip", .tex_top = "block/pink_tulip", .tex_bottom = "block/pink_tulip", .tex_side = "block/pink_tulip",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_OXEYE_DAISY] = {.name = "Oxeye Daisy", .tex_top = "block/oxeye_daisy", .tex_bottom = "block/oxeye_daisy", .tex_side = "block/oxeye_daisy",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS},
    [BLOCK_FERN] = {.name = "Fern", .tex_top = "block/fern", .tex_bottom = "block/fern", .tex_side = "block/fern",
        .solid = false, .opaque = false, .shape = SHAPE_CROSS, .tint_mode = TINT_ALL, .tint_r = 0.40f, .tint_g = 0.75f, .tint_b = 0.30f},
};

uint16_t g_block_tiles[BLOCK_COUNT][FACE_COUNT];

static uint16_t resolve_tile(const Assets *assets, const char *name, int fallback) {
    int tile = assets_tile(assets, name);
    if (tile < 0) {
        fprintf(stderr, "missing texture: %s\n", name);
        return (uint16_t)fallback;
    }
    return (uint16_t)tile;
}

void blocks_init(const Assets *assets) {
    int fallback = assets_tile(assets, "block/stone");
    if (fallback < 0)
        fallback = 0;
    for (int i = 1; i < BLOCK_COUNT; i++) {
        const BlockDef *d = &g_block_defs[i];
        const char *names[FACE_COUNT] = {
            d->tex_top, d->tex_bottom, d->tex_side, d->tex_side, d->tex_side, d->tex_side};
        if (d->tex_front && d->tex_front[0])
            names[FACE_NORTH] = d->tex_front;
        for (int f = 0; f < FACE_COUNT; f++) {
            if (!names[f] || !names[f][0])
                names[f] = "block/stone";
            g_block_tiles[i][f] = resolve_tile(assets, names[f], fallback);
        }
    }
}

float block_hardness(uint8_t id) {
    if (id == BLOCK_AIR || id == BLOCK_TNT || id == BLOCK_TORCH ||
        id == BLOCK_DANDELION || id == BLOCK_POPPY || id == BLOCK_SHORT_GRASS ||
        id == BLOCK_SUGAR_CANE || id == BLOCK_DEAD_BUSH || id == BLOCK_CHERRY_SAPLING ||
        id == BLOCK_REDSTONE_WIRE || id == BLOCK_REDSTONE_TORCH ||
        id == BLOCK_LEVER || id == BLOCK_STONE_BUTTON || id == BLOCK_PALE_HANGING_MOSS ||
        id == BLOCK_SEAGRASS || id == BLOCK_KELP ||
        id == BLOCK_TUBE_CORAL || id == BLOCK_BRAIN_CORAL || id == BLOCK_BUBBLE_CORAL ||
        id == BLOCK_FIRE_CORAL || id == BLOCK_HORN_CORAL ||
        id == BLOCK_CORNFLOWER || id == BLOCK_ALLIUM || id == BLOCK_AZURE_BLUET ||
        id == BLOCK_RED_TULIP || id == BLOCK_ORANGE_TULIP || id == BLOCK_WHITE_TULIP ||
        id == BLOCK_PINK_TULIP || id == BLOCK_OXEYE_DAISY || id == BLOCK_FERN) return 0.0f;
    if (id == BLOCK_BEDROCK) return -1.0f;
    switch (id) {
    case BLOCK_OAK_LEAVES: case BLOCK_BIRCH_LEAVES: case BLOCK_SPRUCE_LEAVES:
    case BLOCK_JUNGLE_LEAVES: case BLOCK_ACACIA_LEAVES: case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_CHERRY_LEAVES: case BLOCK_PALE_OAK_LEAVES:
    case BLOCK_AZALEA_LEAVES: case BLOCK_FLOWERING_AZALEA_LEAVES:
    case BLOCK_CACTUS: case BLOCK_SNOW_BLOCK:
        return 0.25f;

    case BLOCK_GRASS_BLOCK: case BLOCK_DIRT: case BLOCK_COARSE_DIRT:
    case BLOCK_PODZOL: case BLOCK_MYCELIUM: case BLOCK_SAND:
    case BLOCK_RED_SAND: case BLOCK_GRAVEL: case BLOCK_CLAY:
    case BLOCK_SOUL_SAND: case BLOCK_MUD: case BLOCK_PALE_MOSS_BLOCK:
    case BLOCK_SCULK:
        return 0.55f;

    case BLOCK_SULFUR: case BLOCK_CINNABAR: case BLOCK_POTENT_SULFUR: case BLOCK_SULFUR_SPIKE:
    case BLOCK_DRIPSTONE_BLOCK:
    case BLOCK_SCULK_CATALYST: case BLOCK_SCULK_SENSOR: case BLOCK_SCULK_SHRIEKER:
        return 1.50f;

    case BLOCK_CREAKING_HEART:
        return 5.0f;

    case BLOCK_GLASS: case BLOCK_GLOWSTONE: case BLOCK_SEA_LANTERN:
    case BLOCK_ICE: case BLOCK_PACKED_ICE:
        return 0.35f;

    case BLOCK_OAK_LOG: case BLOCK_BIRCH_LOG: case BLOCK_SPRUCE_LOG:
    case BLOCK_JUNGLE_LOG: case BLOCK_ACACIA_LOG: case BLOCK_DARK_OAK_LOG:
    case BLOCK_CHERRY_LOG: case BLOCK_PALE_OAK_LOG:
    case BLOCK_OAK_PLANKS: case BLOCK_BIRCH_PLANKS: case BLOCK_SPRUCE_PLANKS:
    case BLOCK_JUNGLE_PLANKS: case BLOCK_ACACIA_PLANKS: case BLOCK_DARK_OAK_PLANKS:
    case BLOCK_CHERRY_PLANKS: case BLOCK_PALE_OAK_PLANKS:
    case BLOCK_CRAFTING_TABLE: case BLOCK_FURNACE: case BLOCK_CHEST: case BLOCK_BOOKSHELF: case BLOCK_PUMPKIN:
    case BLOCK_CARVED_PUMPKIN: case BLOCK_JACK_O_LANTERN: case BLOCK_MELON:
    case BLOCK_OAK_SLAB: case BLOCK_OAK_STAIRS:
        return 1.35f;

    case BLOCK_TERRACOTTA: case BLOCK_RED_TERRACOTTA: case BLOCK_ORANGE_TERRACOTTA:
    case BLOCK_YELLOW_TERRACOTTA: case BLOCK_BROWN_TERRACOTTA: case BLOCK_WHITE_TERRACOTTA:
    case BLOCK_LIGHT_GRAY_TERRACOTTA:
        return 1.25f;

    case BLOCK_COBBLESTONE_SLAB: case BLOCK_STONE_SLAB: case BLOCK_STONE_BRICK_SLAB:
    case BLOCK_COBBLESTONE_STAIRS: case BLOCK_STONE_BRICK_STAIRS:
    case BLOCK_CALCITE: case BLOCK_AMETHYST_BLOCK: case BLOCK_TUFF: case BLOCK_TUFF_BRICKS:
    case BLOCK_MUD_BRICKS: case BLOCK_PACKED_MUD:
    case BLOCK_TUBE_CORAL_BLOCK: case BLOCK_BRAIN_CORAL_BLOCK:
    case BLOCK_BUBBLE_CORAL_BLOCK: case BLOCK_FIRE_CORAL_BLOCK:
    case BLOCK_HORN_CORAL_BLOCK:
        return 1.5f;

    case BLOCK_BLUE_ICE:
        return 2.8f;

    case BLOCK_CRAFTER:
        return 2.5f;

    case BLOCK_DEEPSLATE: case BLOCK_COBBLED_DEEPSLATE:
    case BLOCK_DEEPSLATE_COAL_ORE: case BLOCK_DEEPSLATE_IRON_ORE:
    case BLOCK_DEEPSLATE_COPPER_ORE: case BLOCK_DEEPSLATE_GOLD_ORE:
    case BLOCK_DEEPSLATE_REDSTONE_ORE: case BLOCK_DEEPSLATE_EMERALD_ORE:
    case BLOCK_DEEPSLATE_LAPIS_ORE: case BLOCK_DEEPSLATE_DIAMOND_ORE:
    case BLOCK_DEEPSLATE_BRICKS: case BLOCK_DEEPSLATE_TILES:
    case BLOCK_POLISHED_DEEPSLATE: case BLOCK_CHISELED_DEEPSLATE:
        return 3.0f;

    case BLOCK_OBSIDIAN:
        return 10.0f;

    case BLOCK_HEAVY_CORE:
        return 12.0f;

    default:
        return 1.75f;
    }
}

ToolType item_tool_type(uint8_t id) {
    switch (id) {
    case ITEM_WOODEN_PICKAXE:
    case ITEM_STONE_PICKAXE:
    case ITEM_IRON_PICKAXE:
    case ITEM_DIAMOND_PICKAXE:
        return TOOL_PICKAXE;
    case ITEM_WOODEN_AXE:
    case ITEM_STONE_AXE:
    case ITEM_IRON_AXE:
    case ITEM_DIAMOND_AXE:
        return TOOL_AXE;
    case ITEM_WOODEN_SHOVEL:
    case ITEM_STONE_SHOVEL:
    case ITEM_IRON_SHOVEL:
    case ITEM_DIAMOND_SHOVEL:
        return TOOL_SHOVEL;
    case ITEM_WOODEN_SWORD:
    case ITEM_STONE_SWORD:
    case ITEM_IRON_SWORD:
    case ITEM_DIAMOND_SWORD:
        return TOOL_SWORD;
    default:
        return TOOL_NONE;
    }
}

int item_tool_tier(uint8_t id) {
    switch (id) {
    case ITEM_WOODEN_PICKAXE:
    case ITEM_WOODEN_AXE:
    case ITEM_WOODEN_SHOVEL:
    case ITEM_WOODEN_SWORD:
        return 1;
    case ITEM_STONE_PICKAXE:
    case ITEM_STONE_AXE:
    case ITEM_STONE_SHOVEL:
    case ITEM_STONE_SWORD:
        return 2;
    case ITEM_IRON_PICKAXE:
    case ITEM_IRON_AXE:
    case ITEM_IRON_SHOVEL:
    case ITEM_IRON_SWORD:
        return 3;
    case ITEM_DIAMOND_PICKAXE:
    case ITEM_DIAMOND_AXE:
    case ITEM_DIAMOND_SHOVEL:
    case ITEM_DIAMOND_SWORD:
        return 4;
    default:
        return 0;
    }
}

ToolType block_required_tool(uint8_t block_id) {
    switch (block_id) {
    case BLOCK_STONE: case BLOCK_GRANITE: case BLOCK_DIORITE: case BLOCK_ANDESITE:
    case BLOCK_DEEPSLATE: case BLOCK_COBBLESTONE: case BLOCK_COBBLED_DEEPSLATE:
    case BLOCK_SANDSTONE: case BLOCK_RED_SANDSTONE:
    case BLOCK_COAL_ORE: case BLOCK_IRON_ORE: case BLOCK_COPPER_ORE:
    case BLOCK_GOLD_ORE: case BLOCK_REDSTONE_ORE: case BLOCK_LAPIS_ORE:
    case BLOCK_DIAMOND_ORE: case BLOCK_EMERALD_ORE:
    case BLOCK_DEEPSLATE_COAL_ORE: case BLOCK_DEEPSLATE_IRON_ORE: case BLOCK_DEEPSLATE_COPPER_ORE:
    case BLOCK_DEEPSLATE_GOLD_ORE: case BLOCK_DEEPSLATE_REDSTONE_ORE: case BLOCK_DEEPSLATE_EMERALD_ORE:
    case BLOCK_DEEPSLATE_LAPIS_ORE: case BLOCK_DEEPSLATE_DIAMOND_ORE:
    case BLOCK_DEEPSLATE_BRICKS: case BLOCK_DEEPSLATE_TILES: case BLOCK_POLISHED_DEEPSLATE:
    case BLOCK_CHISELED_DEEPSLATE:
    case BLOCK_BRICKS: case BLOCK_STONE_BRICKS: case BLOCK_MOSSY_COBBLESTONE:
    case BLOCK_OBSIDIAN: case BLOCK_NETHERRACK: case BLOCK_FURNACE:
    case BLOCK_CALCITE: case BLOCK_TUFF: case BLOCK_TUFF_BRICKS: case BLOCK_AMETHYST_BLOCK:
    case BLOCK_MUD_BRICKS:
    case BLOCK_TUBE_CORAL_BLOCK: case BLOCK_BRAIN_CORAL_BLOCK:
    case BLOCK_BUBBLE_CORAL_BLOCK: case BLOCK_FIRE_CORAL_BLOCK:
    case BLOCK_HORN_CORAL_BLOCK:
    case BLOCK_TERRACOTTA: case BLOCK_RED_TERRACOTTA: case BLOCK_ORANGE_TERRACOTTA:
    case BLOCK_YELLOW_TERRACOTTA: case BLOCK_BROWN_TERRACOTTA: case BLOCK_WHITE_TERRACOTTA:
    case BLOCK_LIGHT_GRAY_TERRACOTTA:
    case BLOCK_BLUE_ICE: case BLOCK_CRAFTER: case BLOCK_HEAVY_CORE:
    case BLOCK_SULFUR: case BLOCK_CINNABAR: case BLOCK_POTENT_SULFUR: case BLOCK_SULFUR_SPIKE:
    case BLOCK_DRIPSTONE_BLOCK: case BLOCK_POINTED_DRIPSTONE:
    case BLOCK_SCULK_CATALYST: case BLOCK_SCULK_SENSOR: case BLOCK_SCULK_SHRIEKER:
    case BLOCK_COPPER_BLOCK: case BLOCK_IRON_BLOCK: case BLOCK_GOLD_BLOCK:
    case BLOCK_DIAMOND_BLOCK: case BLOCK_EMERALD_BLOCK: case BLOCK_LAPIS_BLOCK:
    case BLOCK_REDSTONE_BLOCK: case BLOCK_COAL_BLOCK: case BLOCK_QUARTZ_BLOCK:
    case BLOCK_PURPUR_BLOCK: case BLOCK_END_STONE: case BLOCK_PRISMARINE:
    case BLOCK_DARK_PRISMARINE:
    case BLOCK_COBBLESTONE_SLAB: case BLOCK_STONE_SLAB: case BLOCK_STONE_BRICK_SLAB:
    case BLOCK_COBBLESTONE_STAIRS: case BLOCK_STONE_BRICK_STAIRS:
        return TOOL_PICKAXE;

    case BLOCK_OAK_LOG: case BLOCK_BIRCH_LOG: case BLOCK_SPRUCE_LOG:
    case BLOCK_JUNGLE_LOG: case BLOCK_ACACIA_LOG: case BLOCK_DARK_OAK_LOG:
    case BLOCK_CHERRY_LOG: case BLOCK_PALE_OAK_LOG: case BLOCK_CREAKING_HEART:
    case BLOCK_OAK_PLANKS: case BLOCK_BIRCH_PLANKS: case BLOCK_SPRUCE_PLANKS:
    case BLOCK_JUNGLE_PLANKS: case BLOCK_ACACIA_PLANKS: case BLOCK_DARK_OAK_PLANKS:
    case BLOCK_CHERRY_PLANKS: case BLOCK_PALE_OAK_PLANKS:
    case BLOCK_CRAFTING_TABLE: case BLOCK_BOOKSHELF: case BLOCK_CHEST:
    case BLOCK_PUMPKIN: case BLOCK_CARVED_PUMPKIN: case BLOCK_JACK_O_LANTERN:
    case BLOCK_MELON:
    case BLOCK_OAK_SLAB: case BLOCK_OAK_STAIRS:
        return TOOL_AXE;

    case BLOCK_DIRT: case BLOCK_COARSE_DIRT: case BLOCK_GRASS_BLOCK:
    case BLOCK_PODZOL: case BLOCK_MYCELIUM: case BLOCK_SAND:
    case BLOCK_RED_SAND: case BLOCK_GRAVEL: case BLOCK_CLAY:
    case BLOCK_SOUL_SAND: case BLOCK_SNOW_BLOCK: case BLOCK_MUD: case BLOCK_PACKED_MUD:
    case BLOCK_PALE_MOSS_BLOCK:
        return TOOL_SHOVEL;

    default:
        return TOOL_NONE;
    }
}

bool block_can_harvest(uint8_t block_id, uint8_t held_item) {
    if (block_id == BLOCK_BEDROCK) return false;

    ToolType req = block_required_tool(block_id);
    if (req != TOOL_PICKAXE) {
        return true;
    }

    ToolType held_tool = item_tool_type(held_item);
    if (held_tool != TOOL_PICKAXE) return false;
    int tier = item_tool_tier(held_item);

    switch (block_id) {
    case BLOCK_OBSIDIAN:
        return tier >= 4;

    case BLOCK_DIAMOND_ORE:
    case BLOCK_DIAMOND_BLOCK:
    case BLOCK_DEEPSLATE_DIAMOND_ORE:
    case BLOCK_GOLD_ORE:
    case BLOCK_GOLD_BLOCK:
    case BLOCK_DEEPSLATE_GOLD_ORE:
    case BLOCK_REDSTONE_ORE:
    case BLOCK_REDSTONE_BLOCK:
    case BLOCK_DEEPSLATE_REDSTONE_ORE:
    case BLOCK_EMERALD_ORE:
    case BLOCK_EMERALD_BLOCK:
    case BLOCK_DEEPSLATE_EMERALD_ORE:
        return tier >= 3;

    case BLOCK_IRON_ORE:
    case BLOCK_IRON_BLOCK:
    case BLOCK_DEEPSLATE_IRON_ORE:
    case BLOCK_COPPER_ORE:
    case BLOCK_COPPER_BLOCK:
    case BLOCK_DEEPSLATE_COPPER_ORE:
    case BLOCK_LAPIS_ORE:
    case BLOCK_LAPIS_BLOCK:
    case BLOCK_DEEPSLATE_LAPIS_ORE:
        return tier >= 2;

    default:
        return tier >= 1;
    }
}

float block_break_time(uint8_t block_id, uint8_t held_item) {
    float h = block_hardness(block_id);
    if (h < 0.0f) return -1.0f;
    if (h == 0.0f) return 0.0f;

    ToolType req = block_required_tool(block_id);
    ToolType held_t = item_tool_type(held_item);
    int tier = item_tool_tier(held_item);

    float speed = 1.0f;
    if (held_t != TOOL_NONE && held_t == req) {
        switch (tier) {
        case 1: speed = 2.0f; break;
        case 2: speed = 4.0f; break;
        case 3: speed = 6.0f; break;
        case 4: speed = 8.0f; break;
        default: speed = 1.0f; break;
        }
    }

    bool can_harv = block_can_harvest(block_id, held_item);
    if (req == TOOL_PICKAXE && !can_harv) {
        return (h * 5.0f) / speed;
    }

    return (h * 1.5f) / speed;
}

uint8_t block_drop_item(uint8_t block_id, uint8_t held_item) {
    if (!block_can_harvest(block_id, held_item)) {
        return BLOCK_AIR;
    }
    switch (block_id) {
    case BLOCK_STONE: return BLOCK_COBBLESTONE;
    case BLOCK_DEEPSLATE: return BLOCK_COBBLED_DEEPSLATE;
    case BLOCK_COAL_ORE:
    case BLOCK_DEEPSLATE_COAL_ORE: return ITEM_COAL;
    case BLOCK_IRON_ORE:
    case BLOCK_DEEPSLATE_IRON_ORE: return ITEM_RAW_IRON;
    case BLOCK_COPPER_ORE:
    case BLOCK_DEEPSLATE_COPPER_ORE: return ITEM_RAW_COPPER;
    case BLOCK_GOLD_ORE:
    case BLOCK_DEEPSLATE_GOLD_ORE: return ITEM_RAW_GOLD;
    case BLOCK_DIAMOND_ORE:
    case BLOCK_DEEPSLATE_DIAMOND_ORE: return ITEM_DIAMOND;
    case BLOCK_REDSTONE_ORE:
    case BLOCK_DEEPSLATE_REDSTONE_ORE: return ITEM_REDSTONE;
    case BLOCK_LAPIS_ORE:
    case BLOCK_DEEPSLATE_LAPIS_ORE: return ITEM_LAPIS_LAZULI;
    case BLOCK_EMERALD_ORE:
    case BLOCK_DEEPSLATE_EMERALD_ORE: return ITEM_EMERALD;
    case BLOCK_CHERRY_LEAVES:
        return ((rand() % 100) < 5) ? BLOCK_CHERRY_SAPLING : BLOCK_AIR;
    case BLOCK_PALE_OAK_LEAVES:
        return ((rand() % 100) < 5) ? ITEM_STICK : BLOCK_AIR;
    case BLOCK_AZALEA_LEAVES:
    case BLOCK_FLOWERING_AZALEA_LEAVES:
        return ((rand() % 100) < 5) ? ITEM_STICK : BLOCK_AIR;
    case BLOCK_SULFUR: return BLOCK_SULFUR;
    case BLOCK_CINNABAR: return BLOCK_CINNABAR;
    case BLOCK_POTENT_SULFUR: return BLOCK_POTENT_SULFUR;
    case BLOCK_SULFUR_SPIKE: return BLOCK_SULFUR_SPIKE;
    case BLOCK_CREAKING_HEART: return BLOCK_CREAKING_HEART;
    case BLOCK_PALE_HANGING_MOSS: return BLOCK_PALE_HANGING_MOSS;
    case BLOCK_DRIPSTONE_BLOCK: return BLOCK_DRIPSTONE_BLOCK;
    case BLOCK_POINTED_DRIPSTONE: return BLOCK_POINTED_DRIPSTONE;
    case BLOCK_SCULK: return BLOCK_SCULK;
    case BLOCK_SCULK_CATALYST: return BLOCK_SCULK_CATALYST;
    case BLOCK_SCULK_SENSOR: return BLOCK_SCULK_SENSOR;
    case BLOCK_SCULK_SHRIEKER: return BLOCK_SCULK_SHRIEKER;
    case BLOCK_OAK_DOOR_OPEN: return BLOCK_OAK_DOOR;
    case BLOCK_IRON_DOOR_OPEN: return BLOCK_IRON_DOOR;
    case BLOCK_OAK_TRAPDOOR_OPEN: return BLOCK_OAK_TRAPDOOR;
    case BLOCK_REDSTONE_LAMP_ON: return BLOCK_REDSTONE_LAMP_OFF;
    case BLOCK_REDSTONE_WIRE: return ITEM_REDSTONE;
    case BLOCK_DANDELION: return BLOCK_DANDELION;
    case BLOCK_POPPY: return BLOCK_POPPY;
    case BLOCK_SUGAR_CANE: return ITEM_SUGAR_CANE;
    case BLOCK_SHORT_GRASS:
        return ((rand() % 100) < 15) ? ITEM_WHEAT_SEEDS : BLOCK_AIR;
    case BLOCK_DEAD_BUSH:
        return ((rand() % 100) < 50) ? ITEM_STICK : BLOCK_AIR;
    case BLOCK_GRASS_BLOCK: return BLOCK_DIRT;
    case BLOCK_OAK_LEAVES:
        {
            int r = rand() % 100;
            if (r < 3) return ITEM_APPLE;
            if (r < 10) return ITEM_STICK;
            return BLOCK_AIR;
        }
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_SPRUCE_LEAVES:
    case BLOCK_JUNGLE_LEAVES:
    case BLOCK_ACACIA_LEAVES:
    case BLOCK_DARK_OAK_LEAVES:
        {
            int r = rand() % 100;
            if (r < 10) return ITEM_STICK;
            return BLOCK_AIR;
        }
    default:
        return block_id;
    }
}

bool item_is_food(uint8_t id, float *out_food, float *out_saturation) {
    float f = 0.0f, s = 0.0f;
    switch (id) {
    case ITEM_APPLE:
        f = 4.0f; s = 2.4f;
        break;
    case ITEM_BREAD:
        f = 5.0f; s = 6.0f;
        break;
    case ITEM_COOKED_BEEF:
        f = 8.0f; s = 12.8f;
        break;
    case ITEM_ROTTEN_FLESH:
        f = 4.0f; s = 0.8f;
        break;
    case BLOCK_MELON:
        f = 2.0f; s = 1.2f;
        break;
    default:
        return false;
    }
    if (out_food) *out_food = f;
    if (out_saturation) *out_saturation = s;
    return true;
}

bool item_is_fuel(uint8_t id, float *out_burn_time) {
    float b = 0.0f;
    switch (id) {
    case ITEM_COAL:
    case ITEM_CHARCOAL:
        b = 80.0f;
        break;
    case BLOCK_COAL_BLOCK:
        b = 800.0f;
        break;
    case ITEM_LAVA_BUCKET:
        b = 1000.0f;
        break;
    case BLOCK_OAK_LOG: case BLOCK_BIRCH_LOG: case BLOCK_SPRUCE_LOG:
    case BLOCK_JUNGLE_LOG: case BLOCK_ACACIA_LOG: case BLOCK_DARK_OAK_LOG:
    case BLOCK_OAK_PLANKS: case BLOCK_BIRCH_PLANKS: case BLOCK_SPRUCE_PLANKS:
    case BLOCK_JUNGLE_PLANKS: case BLOCK_ACACIA_PLANKS: case BLOCK_DARK_OAK_PLANKS:
    case BLOCK_CRAFTING_TABLE: case BLOCK_BOOKSHELF: case BLOCK_CHEST:
        b = 15.0f;
        break;
    case ITEM_STICK:
        b = 5.0f;
        break;
    case ITEM_WOODEN_PICKAXE:
    case ITEM_WOODEN_AXE:
    case ITEM_WOODEN_SHOVEL:
    case ITEM_WOODEN_SWORD:
        b = 10.0f;
        break;
    default:
        return false;
    }
    if (out_burn_time) *out_burn_time = b;
    return true;
}

uint8_t item_smelt_result(uint8_t id) {
    switch (id) {
    case BLOCK_COBBLESTONE: return BLOCK_STONE;
    case BLOCK_COBBLED_DEEPSLATE: return BLOCK_DEEPSLATE;
    case BLOCK_SAND: case BLOCK_RED_SAND: return BLOCK_GLASS;
    case BLOCK_IRON_ORE: case ITEM_RAW_IRON: return ITEM_IRON_INGOT;
    case BLOCK_GOLD_ORE: case ITEM_RAW_GOLD: return ITEM_GOLD_INGOT;
    case BLOCK_OAK_LOG: case BLOCK_BIRCH_LOG: case BLOCK_SPRUCE_LOG:
    case BLOCK_JUNGLE_LOG: case BLOCK_ACACIA_LOG: case BLOCK_DARK_OAK_LOG:
        return ITEM_CHARCOAL;
    case BLOCK_CLAY: return BLOCK_BRICKS;
    default:
        return BLOCK_AIR;
    }
}

bool item_is_armor(uint8_t item) {
    switch (item) {
    case ITEM_IRON_HELMET: case ITEM_IRON_CHESTPLATE:
    case ITEM_IRON_LEGGINGS: case ITEM_IRON_BOOTS:
    case ITEM_DIAMOND_HELMET: case ITEM_DIAMOND_CHESTPLATE:
    case ITEM_DIAMOND_LEGGINGS: case ITEM_DIAMOND_BOOTS:
        return true;
    default:
        return false;
    }
}

int armor_slot_for_item(uint8_t item) {
    switch (item) {
    case ITEM_IRON_HELMET: case ITEM_DIAMOND_HELMET:
        return 0; /* Helmet slot */
    case ITEM_IRON_CHESTPLATE: case ITEM_DIAMOND_CHESTPLATE:
        return 1; /* Chestplate slot */
    case ITEM_IRON_LEGGINGS: case ITEM_DIAMOND_LEGGINGS:
        return 2; /* Leggings slot */
    case ITEM_IRON_BOOTS: case ITEM_DIAMOND_BOOTS:
        return 3; /* Boots slot */
    default:
        return -1;
    }
}

int armor_points_for_item(uint8_t item) {
    switch (item) {
    case ITEM_IRON_HELMET: return 2;
    case ITEM_IRON_CHESTPLATE: return 6;
    case ITEM_IRON_LEGGINGS: return 5;
    case ITEM_IRON_BOOTS: return 2;
    case ITEM_DIAMOND_HELMET: return 3;
    case ITEM_DIAMOND_CHESTPLATE: return 8;
    case ITEM_DIAMOND_LEGGINGS: return 6;
    case ITEM_DIAMOND_BOOTS: return 3;
    default: return 0;
    }
}

#include "mced.h"
#include "blocks.h"
#include "player.h"
#include "settings.h"
#include "ui.h"
#include "world.h"
#include "entity.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_creative_invulnerability(void) {
    Player p;
    memset(&p, 0, sizeof(p));
    p.health = PLAYER_MAX_HEALTH;
    p.air = PLAYER_MAX_AIR;

    /* Survival damage test */
    p.creative = false;
    player_damage(&p, 5.0f);
    assert(p.health == 15.0f);
    assert(p.hurt_timer > 0.0f);

    /* Creative damage immunity test */
    p.health = PLAYER_MAX_HEALTH;
    p.hurt_timer = 0.0f;
    p.creative = true;
    player_damage(&p, 10.0f);
    assert(p.health == PLAYER_MAX_HEALTH);
    assert(p.hurt_timer == 0.0f);

    /* Huge void / lethal damage immunity */
    player_damage(&p, 9999.0f);
    assert(p.health == PLAYER_MAX_HEALTH);
    assert(!p.dead);

    printf("PASS: test_creative_invulnerability\n");
}

static void test_chat_system(void) {
    UI ui;
    memset(&ui, 0, sizeof(ui));
    ui_init(&ui, 640, 360);

    /* Test opening chat */
    ui_chat_open(&ui, "");
    assert(ui.screen == UI_SCREEN_CHAT);
    assert(ui.chat_input_len == 0);

    /* Test character input */
    ui_chat_input_char(&ui, '/');
    ui_chat_input_char(&ui, 'g');
    ui_chat_input_char(&ui, 'a');
    ui_chat_input_char(&ui, 'm');
    ui_chat_input_char(&ui, 'e');
    ui_chat_input_char(&ui, 'm');
    ui_chat_input_char(&ui, 'o');
    ui_chat_input_char(&ui, 'd');
    ui_chat_input_char(&ui, 'e');
    assert(strcmp(ui.chat_input, "/gamemode") == 0);
    assert(ui.chat_input_len == 9);

    /* Test backspace */
    ui_chat_backspace(&ui);
    assert(strcmp(ui.chat_input, "/gamemod") == 0);
    assert(ui.chat_input_len == 8);

    /* Test opening chat with prefilled text (e.g. on pressing '/') */
    ui_chat_open(&ui, "/");
    assert(strcmp(ui.chat_input, "/") == 0);
    assert(ui.chat_input_len == 1);

    /* Test chat message history and fading */
    ui_chat_add(&ui, "Hello Steve", (Color3){1, 1, 1});
    assert(ui.chat_msg_count == 1);
    assert(strcmp(ui.chat_msgs[0].text, "Hello Steve") == 0);
    assert(ui.chat_msgs[0].time == 10.0f);

    ui_chat_update(&ui, 2.5f);
    assert(ui.chat_msgs[0].time == 7.5f);

    printf("PASS: test_chat_system\n");
}

static void test_sky_cycle(void) {
    /* Day cycle: 1200 seconds = 20 minutes (vanilla Minecraft day) */
    /* 0s = sunrise, 300s = noon, 600s = sunset, 900s = midnight */
    float times[4] = {0.0f, 300.0f, 600.0f, 900.0f};

    for (int i = 0; i < 4; i++) {
        float cycle = fmodf(times[i], 1200.0f);
        float celestial_angle = ((cycle - 300.0f) / 1200.0f) * 2.0f * (float)M_PI;
        float sun_elev = cosf(celestial_angle);
        float deg = celestial_angle * (180.0f / (float)M_PI);

        if (i == 0) {
            /* Sunrise: sun on horizon */
            assert(fabsf(sun_elev) < 0.001f);
            assert(fabsf(deg - (-90.0f)) < 0.001f);
        } else if (i == 1) {
            /* Noon: sun directly at zenith */
            assert(fabsf(sun_elev - 1.0f) < 0.001f);
            assert(fabsf(deg - 0.0f) < 0.001f);
        } else if (i == 2) {
            /* Sunset: sun on horizon */
            assert(fabsf(sun_elev) < 0.001f);
            assert(fabsf(deg - 90.0f) < 0.001f);
        } else if (i == 3) {
            /* Midnight: sun at nadir, moon directly overhead */
            assert(fabsf(sun_elev - (-1.0f)) < 0.001f);
            assert(fabsf(deg - 180.0f) < 0.001f);
        }
    }
    printf("PASS: test_sky_cycle\n");
}

static void test_spatial_hash_and_chunks(void) {
    World w;
    memset(&w, 0, sizeof(w));
    world_init(&w, 1337ULL, 16);

    /* Test generating and looking up chunks in a radius */
    for (int cx = -8; cx <= 8; cx++) {
        for (int cz = -8; cz <= 8; cz++) {
            Chunk *c = world_find_chunk(&w, cx, cz, true);
            assert(c != NULL);
            assert(c->cx == cx);
            assert(c->cz == cz);
        }
    }

    /* Fast O(1) retrieval test */
    for (int cx = -8; cx <= 8; cx++) {
        for (int cz = -8; cz <= 8; cz++) {
            Chunk *c = world_find_chunk(&w, cx, cz, false);
            assert(c != NULL);
            assert(c->cx == cx && c->cz == cz);
        }
    }

    /* Non-existent chunk lookup returns NULL */
    assert(world_find_chunk(&w, 999, 999, false) == NULL);

    world_destroy(&w);
    printf("PASS: test_spatial_hash_and_chunks\n");
}

static void test_render_distance_and_fog(void) {
    /* Test render radius ranges: 2 to 32 chunks */
    int radii[4] = {4, 16, 24, 32};
    for (int i = 0; i < 4; i++) {
        int r = radii[i];
        float far = (float)((r + 4) * CHUNK_SIZE);
        if (far < 500.0f) far = 500.0f;
        if (far > 3000.0f) far = 3000.0f;

        float fog_dist = (float)(r * CHUNK_SIZE);
        float fog_start = fog_dist * 0.70f;
        float fog_end = fog_dist * 0.98f;

        assert(far >= fog_end);
        assert(fog_start < fog_end);
        if (r == 16) {
            assert((int)fog_dist == 256);
            assert((int)fog_start == 179);
            assert((int)fog_end == 250);
        } else if (r == 24) {
            assert((int)fog_dist == 384);
            assert((int)fog_start == 268);
            assert((int)fog_end == 376);
        } else if (r == 32) {
            assert((int)fog_dist == 512);
            assert((int)fog_start == 358);
            assert((int)fog_end == 501);
            assert((int)far == 576);
        }
    }
    printf("PASS: test_render_distance_and_fog\n");
}

static void test_settings_persistence(void) {
    Settings s1, s2;
    settings_init_defaults(&s1);
    s1.fov = 95;
    s1.render_distance = 24;
    s1.max_fps = 144;
    s1.vsync = true;
    s1.view_bobbing = false;
    s1.clouds = false;
    s1.fog = true;
    s1.brightness = 0.85f;
    s1.gui_scale = 2;
    s1.fullscreen = true;
    s1.master_volume = 0.65f;
    s1.music_volume = 0.40f;
    s1.blocks_volume = 0.80f;
    s1.mute = true;
    s1.mouse_sensitivity = 1.75f;
    s1.invert_mouse = true;

    const char *test_path = "saves/test_options.txt";
    bool save_ok = settings_save(&s1, test_path);
    assert(save_ok);

    bool load_ok = settings_load(&s2, test_path);
    assert(load_ok);

    assert(s2.fov == 95);
    assert(s2.render_distance == 24);
    assert(s2.max_fps == 144);
    assert(s2.vsync == true);
    assert(s2.view_bobbing == false);
    assert(s2.clouds == false);
    assert(s2.fog == true);
    assert(fabsf(s2.brightness - 0.85f) < 0.01f);
    assert(s2.gui_scale == 2);
    assert(s2.fullscreen == true);
    assert(fabsf(s2.master_volume - 0.65f) < 0.01f);
    assert(fabsf(s2.music_volume - 0.40f) < 0.01f);
    assert(fabsf(s2.blocks_volume - 0.80f) < 0.01f);
    assert(s2.mute == true);
    assert(fabsf(s2.mouse_sensitivity - 1.75f) < 0.01f);
    assert(s2.invert_mouse == true);

    remove(test_path);
    printf("PASS: test_settings_persistence\n");
}

static void test_ui_screens(void) {
    UI ui;
    Settings s;
    settings_init_defaults(&s);
    memset(&ui, 0, sizeof(ui));
    ui_init(&ui, 854, 480);
    ui_set_settings(&ui, &s);

    /* Title screen */
    ui_set_screen(&ui, UI_SCREEN_TITLE, true);
    assert(ui.screen == UI_SCREEN_TITLE);
    assert(ui.button_count >= 4);

    /* Options screen */
    ui_set_screen(&ui, UI_SCREEN_OPTIONS, true);
    assert(ui.screen == UI_SCREEN_OPTIONS);
    assert(ui.button_count >= 5);

    /* Video Settings */
    ui_set_screen(&ui, UI_SCREEN_VIDEO, true);
    assert(ui.screen == UI_SCREEN_VIDEO);
    assert(ui.button_count >= 8);

    /* Audio Settings */
    ui_set_screen(&ui, UI_SCREEN_AUDIO, true);
    assert(ui.screen == UI_SCREEN_AUDIO);
    assert(ui.button_count >= 5);

    /* Controls Settings */
    ui_set_screen(&ui, UI_SCREEN_CONTROLS, true);
    assert(ui.screen == UI_SCREEN_CONTROLS);
    assert(ui.button_count >= 3);

    /* Advancements screen */
    ui_set_screen(&ui, UI_SCREEN_ADVANCEMENTS, true);
    assert(ui.screen == UI_SCREEN_ADVANCEMENTS);
    assert(ui.button_count >= 1);

    /* Pause Menu */
    ui_set_screen(&ui, UI_SCREEN_PAUSE, true);
    assert(ui.screen == UI_SCREEN_PAUSE);
    assert(ui.button_count >= 4);

    /* Create World Screen */
    ui_set_screen(&ui, UI_SCREEN_CREATE_WORLD, true);
    assert(ui.screen == UI_SCREEN_CREATE_WORLD);
    assert(ui.button_count >= 5);

    /* Text input test on Create World */
    ui.focused_text_field = 1;
    ui.world_name[0] = '\0';
    ui.world_name_len = 0;
    ui_text_input_char(&ui, 'M');
    ui_text_input_char(&ui, 'y');
    ui_text_input_char(&ui, 'W');
    assert(strcmp(ui.world_name, "MyW") == 0);
    ui_text_input_backspace(&ui);
    assert(strcmp(ui.world_name, "My") == 0);

    printf("PASS: test_ui_screens\n");
}

static void test_inventory_and_crafting(void) {
    Player p;
    player_init(&p, vec3(0, 64, 0));

    /* Test 1: Log to 4 Planks crafting */
    p.crafting[0] = BLOCK_OAK_LOG;
    p.crafting_counts[0] = 1;
    player_update_crafting(&p);
    assert(p.craft_result == BLOCK_OAK_PLANKS);
    assert(p.craft_result_count == 4);

    /* Craft result click: pickup planks, consume log */
    player_slot_click(&p, SLOT_CRAFT_RESULT, 0, false, false);
    assert(p.cursor_item == BLOCK_OAK_PLANKS);
    assert(p.cursor_count == 4);
    assert(p.crafting[0] == BLOCK_AIR);
    assert(p.craft_result == BLOCK_AIR);

    /* Test 2: Two vertical planks to 4 Sticks */
    p.cursor_item = BLOCK_AIR;
    p.cursor_count = 0;
    p.crafting[0] = BLOCK_OAK_PLANKS;
    p.crafting_counts[0] = 1;
    p.crafting[2] = BLOCK_OAK_PLANKS;
    p.crafting_counts[2] = 1;
    player_update_crafting(&p);
    assert(p.craft_result == BLOCK_TORCH);
    assert(p.craft_result_count == 4);

    /* Test 3: Four planks to 1 Crafting Table */
    p.crafting[0] = BLOCK_OAK_PLANKS; p.crafting_counts[0] = 1;
    p.crafting[1] = BLOCK_OAK_PLANKS; p.crafting_counts[1] = 1;
    p.crafting[2] = BLOCK_OAK_PLANKS; p.crafting_counts[2] = 1;
    p.crafting[3] = BLOCK_OAK_PLANKS; p.crafting_counts[3] = 1;
    player_update_crafting(&p);
    assert(p.craft_result == BLOCK_CRAFTING_TABLE);
    assert(p.craft_result_count == 1);

    /* Test 4: Inventory slot click actions */
    /* Place 64 stone into hotbar slot 0 */
    p.hotbar[0] = BLOCK_STONE;
    p.hotbar_counts[0] = 64;
    p.cursor_item = BLOCK_AIR;
    p.cursor_count = 0;

    /* Right click hotbar slot 0: should pick up 32 (half) */
    player_slot_click(&p, SLOT_HOTBAR, 0, true, false);
    assert(p.hotbar_counts[0] == 32);
    assert(p.cursor_item == BLOCK_STONE);
    assert(p.cursor_count == 32);

    /* Right click empty hotbar slot 1: should place 1 item */
    player_slot_click(&p, SLOT_HOTBAR, 1, true, false);
    assert(p.hotbar[1] == BLOCK_STONE);
    assert(p.hotbar_counts[1] == 1);
    assert(p.cursor_count == 31);

    /* Shift-click hotbar slot 1: should transfer to first empty inventory slot */
    player_slot_click(&p, SLOT_HOTBAR, 1, false, true);
    assert(p.hotbar[1] == BLOCK_AIR);
    assert(p.inventory[0] == BLOCK_STONE);
    assert(p.inventory_counts[0] == 1);

    printf("PASS: test_inventory_and_crafting\n");
}

static void test_survival_mechanics(void) {
    Player p;
    player_init(&p, vec3(0, 64, 0));
    p.creative = false;

    /* Test 1: Armor damage reduction */
    /* 20 armor points = 80% damage reduction */
    p.health = 20.0f;
    p.armor_points = 20;
    player_damage(&p, 10.0f);
    /* 10 damage reduced by 80% = 2.0 damage dealt */
    assert(fabsf(p.health - 18.0f) < 0.01f);

    World w;
    world_init(&w, 42, 2);
    PlayerInput in = {0};

    /* Test 2: Natural regeneration when hunger >= 18 */
    p.hunger = 20.0f;
    p.saturation = 5.0f;
    p.health = 15.0f;
    p.regen_timer = 4.1f; /* trigger regen */
    player_update(&p, &w, &in, 0.05f);
    assert(p.health == 16.0f);

    /* Test 3: Starvation damage when hunger <= 0 */
    p.hunger = 0.0f;
    p.health = 20.0f;
    p.regen_timer = 4.1f; /* trigger starvation */
    player_update(&p, &w, &in, 0.05f);
    assert(p.health == 19.0f);

    world_destroy(&w);
    printf("PASS: test_survival_mechanics\n");
}

static void test_tools_and_mining(void) {
    /* 1. Tool types and tiers */
    assert(item_tool_type(ITEM_WOODEN_PICKAXE) == TOOL_PICKAXE);
    assert(item_tool_tier(ITEM_WOODEN_PICKAXE) == 1);
    assert(item_tool_type(ITEM_STONE_PICKAXE) == TOOL_PICKAXE);
    assert(item_tool_tier(ITEM_STONE_PICKAXE) == 2);
    assert(item_tool_type(ITEM_IRON_PICKAXE) == TOOL_PICKAXE);
    assert(item_tool_tier(ITEM_IRON_PICKAXE) == 3);
    assert(item_tool_type(ITEM_DIAMOND_PICKAXE) == TOOL_PICKAXE);
    assert(item_tool_tier(ITEM_DIAMOND_PICKAXE) == 4);

    assert(item_tool_type(ITEM_DIAMOND_AXE) == TOOL_AXE);
    assert(item_tool_type(ITEM_DIAMOND_SHOVEL) == TOOL_SHOVEL);
    assert(item_tool_type(ITEM_DIAMOND_SWORD) == TOOL_SWORD);
    assert(item_tool_type(ITEM_STICK) == TOOL_NONE);

    /* 2. Harvest eligibility */
    /* Stone requires pickaxe tier >= 1 */
    assert(!block_can_harvest(BLOCK_STONE, BLOCK_AIR));
    assert(!block_can_harvest(BLOCK_STONE, ITEM_STICK));
    assert(block_can_harvest(BLOCK_STONE, ITEM_WOODEN_PICKAXE));
    assert(block_can_harvest(BLOCK_STONE, ITEM_DIAMOND_PICKAXE));

    /* Diamond Ore requires pickaxe tier >= 3 (Iron or Diamond) */
    assert(!block_can_harvest(BLOCK_DIAMOND_ORE, ITEM_WOODEN_PICKAXE));
    assert(!block_can_harvest(BLOCK_DIAMOND_ORE, ITEM_STONE_PICKAXE));
    assert(block_can_harvest(BLOCK_DIAMOND_ORE, ITEM_IRON_PICKAXE));
    assert(block_can_harvest(BLOCK_DIAMOND_ORE, ITEM_DIAMOND_PICKAXE));

    /* Obsidian requires pickaxe tier >= 4 (Diamond) */
    assert(!block_can_harvest(BLOCK_OBSIDIAN, ITEM_IRON_PICKAXE));
    assert(block_can_harvest(BLOCK_OBSIDIAN, ITEM_DIAMOND_PICKAXE));

    /* Dirt and Wood do not require pickaxes to harvest */
    assert(block_can_harvest(BLOCK_DIRT, BLOCK_AIR));
    assert(block_can_harvest(BLOCK_OAK_LOG, BLOCK_AIR));

    /* 3. Mining break times */
    float hand_stone = block_break_time(BLOCK_STONE, BLOCK_AIR);
    assert(fabsf(hand_stone - 7.5f) < 0.001f); /* 1.5 * 5.0 */

    float wood_pick_stone = block_break_time(BLOCK_STONE, ITEM_WOODEN_PICKAXE);
    assert(fabsf(wood_pick_stone - 1.125f) < 0.001f); /* (1.5 * 1.5) / 2.0 */

    float dia_pick_stone = block_break_time(BLOCK_STONE, ITEM_DIAMOND_PICKAXE);
    assert(fabsf(dia_pick_stone - 0.28125f) < 0.001f); /* (1.5 * 1.5) / 8.0 */

    /* 4. Block drops */
    assert(block_drop_item(BLOCK_STONE, BLOCK_AIR) == BLOCK_AIR);
    assert(block_drop_item(BLOCK_STONE, ITEM_WOODEN_PICKAXE) == BLOCK_COBBLESTONE);
    assert(block_drop_item(BLOCK_COAL_ORE, ITEM_WOODEN_PICKAXE) == ITEM_COAL);
    assert(block_drop_item(BLOCK_IRON_ORE, ITEM_STONE_PICKAXE) == ITEM_RAW_IRON);
    assert(block_drop_item(BLOCK_DIAMOND_ORE, ITEM_STONE_PICKAXE) == BLOCK_AIR);
    assert(block_drop_item(BLOCK_DIAMOND_ORE, ITEM_IRON_PICKAXE) == ITEM_DIAMOND);
    assert(block_drop_item(BLOCK_DIRT, BLOCK_AIR) == BLOCK_DIRT);

    printf("PASS: test_tools_and_mining\n");
}

static void test_crafting_3x3(void) {
    Player p;
    player_init(&p, vec3(0, 64, 0));

    /* 1. Test 3x3 Wooden Pickaxe */
    p.crafting_3x3[0] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[0] = 1;
    p.crafting_3x3[1] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[1] = 1;
    p.crafting_3x3[2] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[2] = 1;
    p.crafting_3x3[4] = ITEM_STICK;       p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[7] = ITEM_STICK;       p.crafting_3x3_counts[7] = 1;

    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_WOODEN_PICKAXE);
    assert(p.craft_3x3_result_count == 1);

    /* Test clicking result */
    player_slot_click_crafting_3x3(&p, SLOT_CRAFT_3X3_RESULT, 0, false, false);
    assert(p.cursor_item == ITEM_WOODEN_PICKAXE);
    assert(p.cursor_count == 1);
    assert(p.craft_3x3_result == BLOCK_AIR);
    for (int i = 0; i < 9; i++) {
        assert(p.crafting_3x3[i] == BLOCK_AIR);
    }
    p.cursor_item = BLOCK_AIR;
    p.cursor_count = 0;

    /* 2. Test Diamond Sword */
    p.crafting_3x3[1] = ITEM_DIAMOND; p.crafting_3x3_counts[1] = 1;
    p.crafting_3x3[4] = ITEM_DIAMOND; p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[7] = ITEM_STICK;   p.crafting_3x3_counts[7] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_DIAMOND_SWORD);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));
    memset(p.crafting_3x3_counts, 0, sizeof(p.crafting_3x3_counts));

    /* 3. Test Furnace Recipe (8 Cobblestone around center) */
    int ring[8] = {0, 1, 2, 3, 5, 6, 7, 8};
    for (int i = 0; i < 8; i++) {
        p.crafting_3x3[ring[i]] = BLOCK_COBBLESTONE;
        p.crafting_3x3_counts[ring[i]] = 1;
    }
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_FURNACE);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));
    memset(p.crafting_3x3_counts, 0, sizeof(p.crafting_3x3_counts));

    /* 4. Test Bread (3 Wheat horizontally) */
    p.crafting_3x3[3] = ITEM_WHEAT; p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[4] = ITEM_WHEAT; p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[5] = ITEM_WHEAT; p.crafting_3x3_counts[5] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_BREAD);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));
    memset(p.crafting_3x3_counts, 0, sizeof(p.crafting_3x3_counts));

    /* 5. Ingot compression & decompression */
    for (int i = 0; i < 9; i++) {
        p.crafting_3x3[i] = ITEM_IRON_INGOT;
        p.crafting_3x3_counts[i] = 1;
    }
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_IRON_BLOCK);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));
    memset(p.crafting_3x3_counts, 0, sizeof(p.crafting_3x3_counts));

    p.crafting_3x3[0] = BLOCK_IRON_BLOCK;
    p.crafting_3x3_counts[0] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_IRON_INGOT);
    assert(p.craft_3x3_result_count == 9);

    printf("PASS: test_crafting_3x3\n");
}

static void test_furnace_smelting(void) {
    /* 1. Smelt recipe lookups */
    assert(item_smelt_result(BLOCK_COBBLESTONE) == BLOCK_STONE);
    assert(item_smelt_result(ITEM_RAW_IRON) == ITEM_IRON_INGOT);
    assert(item_smelt_result(BLOCK_SAND) == BLOCK_GLASS);
    assert(item_smelt_result(BLOCK_OAK_LOG) == ITEM_CHARCOAL);
    assert(item_smelt_result(BLOCK_DIRT) == BLOCK_AIR);

    /* 2. Fuel burn times */
    float dur = 0.0f;
    assert(item_is_fuel(ITEM_COAL, &dur) && dur == 80.0f);
    assert(item_is_fuel(ITEM_STICK, &dur) && dur == 5.0f);
    assert(item_is_fuel(BLOCK_OAK_PLANKS, &dur) && dur == 15.0f);
    assert(!item_is_fuel(BLOCK_STONE, &dur));

    /* 3. Smelting simulation */
    Player p;
    player_init(&p, vec3(0, 64, 0));

    p.furnace_input = BLOCK_COBBLESTONE;
    p.furnace_input_count = 1;
    p.furnace_fuel = ITEM_COAL;
    p.furnace_fuel_count = 1;

    /* Advance 5.0 seconds */
    player_update_furnace(&p, 5.0f);
    assert(p.furnace_fuel == BLOCK_AIR); /* coal consumed */
    assert(fabsf(p.furnace_burn_time - 75.0f) < 0.01f);
    assert(fabsf(p.furnace_cook_time - 5.0f) < 0.01f);
    assert(p.furnace_output == BLOCK_AIR);

    /* Advance another 5.0 seconds -> 10.0s total (smelt finished!) */
    player_update_furnace(&p, 5.0f);
    assert(p.furnace_input == BLOCK_AIR);
    assert(p.furnace_output == BLOCK_STONE);
    assert(p.furnace_output_count == 1);
    assert(fabsf(p.furnace_cook_time - 0.0f) < 0.01f);

    /* Test clicking output */
    player_slot_click_furnace(&p, SLOT_FURNACE_OUTPUT, 0, false, false);
    assert(p.cursor_item == BLOCK_STONE);
    assert(p.cursor_count == 1);
    assert(p.furnace_output == BLOCK_AIR);

    printf("PASS: test_furnace_smelting\n");
}

static void test_food_and_eating(void) {
    float food = 0, sat = 0;
    assert(item_is_food(ITEM_APPLE, &food, &sat));
    assert(food == 4.0f && sat == 2.4f);

    assert(item_is_food(ITEM_BREAD, &food, &sat));
    assert(food == 5.0f && sat == 6.0f);

    assert(item_is_food(ITEM_COOKED_BEEF, &food, &sat));
    assert(food == 8.0f && sat == 12.8f);

    assert(!item_is_food(BLOCK_STONE, &food, &sat));
    assert(!item_is_food(ITEM_STICK, &food, &sat));

    printf("PASS: test_food_and_eating\n");
}

static void test_chest_mechanics(void) {
    Player p;
    memset(&p, 0, sizeof(p));
    p.health = PLAYER_MAX_HEALTH;

    /* 1. Crafting chest recipe: 8 planks in a ring around center */
    for (int i = 0; i < 9; i++) {
        p.crafting_3x3[i] = (i == 4) ? BLOCK_AIR : BLOCK_OAK_PLANKS;
        p.crafting_3x3_counts[i] = (i == 4) ? 0 : 1;
    }
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_CHEST);
    assert(p.craft_3x3_result_count == 1);

    /* 2. Chest slot interaction: move item to chest and retrieve */
    p.cursor_item = ITEM_DIAMOND;
    p.cursor_count = 5;
    player_slot_click_chest(&p, SLOT_CHEST, 0, false, false);
    assert(p.chest[0] == ITEM_DIAMOND);
    assert(p.chest_counts[0] == 5);
    assert(p.cursor_item == BLOCK_AIR);
    assert(p.cursor_count == 0);

    /* Retrieve from chest */
    player_slot_click_chest(&p, SLOT_CHEST, 0, false, false);
    assert(p.cursor_item == ITEM_DIAMOND);
    assert(p.cursor_count == 5);
    assert(p.chest[0] == BLOCK_AIR);
    assert(p.chest_counts[0] == 0);

    /* Right click split into chest */
    player_slot_click_chest(&p, SLOT_CHEST, 1, true, false);
    assert(p.cursor_count == 4);
    assert(p.chest[1] == ITEM_DIAMOND);
    assert(p.chest_counts[1] == 1);

    printf("PASS: test_chest_mechanics\n");
}

static void test_liquid_physics_and_buckets(void) {
    /* 1. Crafting bucket: 3 iron ingots in V-shape */
    Player p;
    player_init(&p, vec3(0, 64, 0));
    p.crafting_3x3[3] = ITEM_IRON_INGOT; p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[7] = ITEM_IRON_INGOT; p.crafting_3x3_counts[7] = 1;
    p.crafting_3x3[5] = ITEM_IRON_INGOT; p.crafting_3x3_counts[5] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_BUCKET);
    assert(p.craft_3x3_result_count == 1);

    /* 2. Furnace lava bucket fuel and bucket return */
    float fuel_dur = 0.0f;
    assert(item_is_fuel(ITEM_LAVA_BUCKET, &fuel_dur));
    assert(fuel_dur == 1000.0f);

    p.furnace_input = BLOCK_COBBLESTONE;
    p.furnace_input_count = 1;
    p.furnace_fuel = ITEM_LAVA_BUCKET;
    p.furnace_fuel_count = 1;
    player_update_furnace(&p, 1.0f);
    assert(p.furnace_fuel == ITEM_BUCKET);
    assert(p.furnace_fuel_count == 1);
    assert(p.furnace_burn_time > 900.0f);

    /* 3. World Liquid Spreading & Infinite Water Source */
    World w;
    world_init(&w, 12345, 2);

    /* Solid base layer at y=10 */
    for (int x = 0; x < 4; x++) {
        for (int z = 0; z < 4; z++) {
            world_set_block(&w, x, 10, z, BLOCK_STONE, false);
            world_set_block(&w, x, 11, z, BLOCK_AIR, false);
        }
    }
    /* Clear queue before controlled test */
    w.liquid_queue_head = 0;
    w.liquid_queue_tail = 0;

    /* Place 2 water sources diagonally in 2x2 hole: (1, 11, 1) and (2, 11, 2) */
    world_set_block(&w, 1, 11, 1, BLOCK_WATER, false);
    world_set_block(&w, 2, 11, 2, BLOCK_WATER, false);
    world_schedule_liquid_tick(&w, 1, 11, 2);
    world_schedule_liquid_tick(&w, 2, 11, 1);

    world_tick_liquids(&w);
    /* (1, 11, 2) and (2, 11, 1) both have 2 water neighbors and solid bottom -> become water sources */
    assert(world_get_block(&w, 1, 11, 2) == BLOCK_WATER);
    assert(world_get_block(&w, 2, 11, 1) == BLOCK_WATER);

    /* 4. Water and Lava interaction */
    world_set_block(&w, 5, 20, 5, BLOCK_LAVA, false);
    world_set_block(&w, 5, 21, 5, BLOCK_WATER, false);
    world_schedule_liquid_tick(&w, 5, 21, 5);
    world_tick_liquids(&w);
    /* Water above Lava turns lava into Obsidian */
    assert(world_get_block(&w, 5, 20, 5) == BLOCK_OBSIDIAN);

    world_destroy(&w);
    printf("PASS: test_liquid_physics_and_buckets\n");
}

static void test_slabs_and_stairs(void) {
    Player p;
    player_init(&p, vec3(0, 64, 0));

    /* 1. Slabs crafting: 3 planks in a horizontal row */
    p.crafting_3x3[3] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[4] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[5] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[5] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_OAK_SLAB);
    assert(p.craft_3x3_result_count == 6);

    /* 2. Slabs crafting: 3 cobblestone in a horizontal row */
    p.crafting_3x3[3] = BLOCK_COBBLESTONE;
    p.crafting_3x3[4] = BLOCK_COBBLESTONE;
    p.crafting_3x3[5] = BLOCK_COBBLESTONE;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_COBBLESTONE_SLAB);
    assert(p.craft_3x3_result_count == 6);

    /* 3. Stairs crafting: 6 cobblestone in stairs shape */
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));
    p.crafting_3x3[0] = BLOCK_COBBLESTONE; p.crafting_3x3_counts[0] = 1;
    p.crafting_3x3[3] = BLOCK_COBBLESTONE; p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[4] = BLOCK_COBBLESTONE; p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[6] = BLOCK_COBBLESTONE; p.crafting_3x3_counts[6] = 1;
    p.crafting_3x3[7] = BLOCK_COBBLESTONE; p.crafting_3x3_counts[7] = 1;
    p.crafting_3x3[8] = BLOCK_COBBLESTONE; p.crafting_3x3_counts[8] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_COBBLESTONE_STAIRS);
    assert(p.craft_3x3_result_count == 4);

    /* 4. Slab collision and step-up */
    World w;
    world_init(&w, 42, 2);
    world_set_block(&w, 0, 5, 0, BLOCK_OAK_SLAB, false);

    /* Standing on top of slab at y = 5.5 should not collide inside it */
    p.pos = vec3(0.5f, 5.5f, 0.5f);
    PlayerInput in;
    memset(&in, 0, sizeof(in));
    player_update(&p, &w, &in, 0.05f);
    assert(p.pos.y >= 5.5f);

    world_destroy(&w);
    printf("PASS: test_slabs_and_stairs\n");
}

static void test_redstone_mechanics_and_doors(void) {
    Player p;
    player_init(&p, vec3(0, 64, 0));

    /* 1. Crafting redstone torch */
    p.crafting_3x3[1] = ITEM_REDSTONE; p.crafting_3x3_counts[1] = 1;
    p.crafting_3x3[4] = ITEM_STICK;    p.crafting_3x3_counts[4] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_REDSTONE_TORCH);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* 2. Crafting lever */
    p.crafting_3x3[1] = ITEM_STICK;        p.crafting_3x3_counts[1] = 1;
    p.crafting_3x3[4] = BLOCK_COBBLESTONE; p.crafting_3x3_counts[4] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_LEVER);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* 3. Crafting redstone lamp */
    p.crafting_3x3[4] = BLOCK_GLOWSTONE; p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[1] = ITEM_REDSTONE;   p.crafting_3x3_counts[1] = 1;
    p.crafting_3x3[3] = ITEM_REDSTONE;   p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[5] = ITEM_REDSTONE;   p.crafting_3x3_counts[5] = 1;
    p.crafting_3x3[7] = ITEM_REDSTONE;   p.crafting_3x3_counts[7] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_REDSTONE_LAMP_OFF);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* 4. Crafting door (6 oak planks) */
    p.crafting_3x3[0] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[0] = 1;
    p.crafting_3x3[1] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[1] = 1;
    p.crafting_3x3[3] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[4] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[6] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[6] = 1;
    p.crafting_3x3[7] = BLOCK_OAK_PLANKS; p.crafting_3x3_counts[7] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == BLOCK_OAK_DOOR);
    assert(p.craft_3x3_result_count == 3);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* 5. Redstone power transmission & line decay */
    World w;
    world_init(&w, 999, 2);
    world_set_block(&w, 10, 5, 10, BLOCK_REDSTONE_TORCH, false);
    world_set_block(&w, 11, 5, 10, BLOCK_REDSTONE_WIRE, false);
    world_set_block(&w, 12, 5, 10, BLOCK_REDSTONE_WIRE, false);
    world_set_block(&w, 13, 5, 10, BLOCK_REDSTONE_WIRE, false);
    world_set_block(&w, 14, 5, 10, BLOCK_REDSTONE_LAMP_OFF, false);
    world_set_block(&w, 11, 5, 11, BLOCK_OAK_DOOR, false);

    /* Update redstone torch */
    world_update_redstone_at(&w, 10, 5, 10);

    /* Verify power decay: 15 (torch) -> 14 (wire1) -> 13 (wire2) -> 12 (wire3) */
    assert(world_get_power(&w, 10, 5, 10) == 15);
    assert(world_get_power(&w, 11, 5, 10) == 14);
    assert(world_get_power(&w, 12, 5, 10) == 13);
    assert(world_get_power(&w, 13, 5, 10) == 12);

    /* Verify lamp was activated by wire3 */
    assert(world_get_block(&w, 14, 5, 10) == BLOCK_REDSTONE_LAMP_ON);

    /* Verify door was opened by wire1 */
    assert(world_get_block(&w, 11, 5, 11) == BLOCK_OAK_DOOR_OPEN);

    world_destroy(&w);
    printf("PASS: test_redstone_mechanics_and_doors\n");
}

static void test_arrow_projectile_physics(void) {
    EntityManager em;
    entity_manager_init(&em, NULL);

    /* 1. Entity arrow spawning */
    Entity *arr = entity_spawn(&em, ENTITY_ARROW, vec3(10.0f, 5.0f, 10.0f));
    assert(arr != NULL);
    assert(arr->active);
    assert(arr->type == ENTITY_ARROW);
    assert(em.count == 1);

    /* 2. Arrow physics & terrain collision */
    World w;
    world_init(&w, 12345, 2);
    world_set_block(&w, 10, 5, 12, BLOCK_STONE, false);
    arr->vel = vec3(0.0f, 0.0f, 20.0f);

    Player p;
    memset(&p, 0, sizeof(p));
    p.pos = vec3(0.0f, 0.0f, 0.0f);

    /* Update arrow to hit stone block at z=12 */
    entity_update_all(&em, &w, &p, NULL, 0.15f, 800.0f);
    assert(!arr->active); /* Arrow despawned on solid block hit */
    assert(em.count == 0);

    world_destroy(&w);
    printf("PASS: test_arrow_projectile_physics\n");
}

static void test_flora_generation_and_armor(void) {
    /* 1. Armor Points & Damage Reduction */
    Player p;
    memset(&p, 0, sizeof(p));
    p.health = 20.0f;
    p.armor[0] = ITEM_IRON_HELMET;
    p.armor[1] = ITEM_IRON_CHESTPLATE;
    p.armor[2] = ITEM_IRON_LEGGINGS;
    p.armor[3] = ITEM_IRON_BOOTS;
    player_update_armor_points(&p);
    assert(p.armor_points == 15); /* 2 + 6 + 5 + 2 */

    /* Incoming 10.0 damage with 15 armor points (60% reduction) -> 4.0 actual damage */
    player_damage(&p, 10.0f);
    assert(fabsf(p.health - 16.0f) < 0.01f);

    /* Equip full diamond set */
    p.armor[0] = ITEM_DIAMOND_HELMET;
    p.armor[1] = ITEM_DIAMOND_CHESTPLATE;
    p.armor[2] = ITEM_DIAMOND_LEGGINGS;
    p.armor[3] = ITEM_DIAMOND_BOOTS;
    player_update_armor_points(&p);
    assert(p.armor_points == 20); /* 3 + 8 + 6 + 3 */

    /* Incoming 10.0 damage with 20 armor points (80% reduction) -> 2.0 actual damage */
    player_damage(&p, 10.0f);
    assert(fabsf(p.health - 14.0f) < 0.01f);

    /* 2. Crafting Armor & Resources in 3x3 */
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* Helmet recipe */
    p.crafting_3x3[0] = ITEM_IRON_INGOT; p.crafting_3x3_counts[0] = 1;
    p.crafting_3x3[1] = ITEM_IRON_INGOT; p.crafting_3x3_counts[1] = 1;
    p.crafting_3x3[2] = ITEM_IRON_INGOT; p.crafting_3x3_counts[2] = 1;
    p.crafting_3x3[3] = ITEM_IRON_INGOT; p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[5] = ITEM_IRON_INGOT; p.crafting_3x3_counts[5] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_IRON_HELMET);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* Chestplate recipe */
    int chest_slots[8] = {0, 2, 3, 4, 5, 6, 7, 8};
    for (int i = 0; i < 8; i++) {
        p.crafting_3x3[chest_slots[i]] = ITEM_DIAMOND;
        p.crafting_3x3_counts[chest_slots[i]] = 1;
    }
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_DIAMOND_CHESTPLATE);
    assert(p.craft_3x3_result_count == 1);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* Paper recipe: 3 sugar canes horizontally */
    p.crafting_3x3[3] = ITEM_SUGAR_CANE; p.crafting_3x3_counts[3] = 1;
    p.crafting_3x3[4] = ITEM_SUGAR_CANE; p.crafting_3x3_counts[4] = 1;
    p.crafting_3x3[5] = ITEM_SUGAR_CANE; p.crafting_3x3_counts[5] = 1;
    player_update_crafting_3x3(&p);
    assert(p.craft_3x3_result == ITEM_PAPER);
    assert(p.craft_3x3_result_count == 3);
    memset(p.crafting_3x3, 0, sizeof(p.crafting_3x3));

    /* Sugar recipe in 2x2 */
    memset(p.crafting, 0, sizeof(p.crafting));
    p.crafting[0] = ITEM_SUGAR_CANE; p.crafting_counts[0] = 1;
    player_update_crafting(&p);
    assert(p.craft_result == ITEM_SUGAR);
    assert(p.craft_result_count == 1);

    /* 3. Flora Block Properties */
    assert(block_hardness(BLOCK_DANDELION) == 0.0f);
    assert(block_hardness(BLOCK_POPPY) == 0.0f);
    assert(block_hardness(BLOCK_SHORT_GRASS) == 0.0f);
    assert(block_hardness(BLOCK_SUGAR_CANE) == 0.0f);
    assert(block_hardness(BLOCK_DEAD_BUSH) == 0.0f);
    assert(!block_is_solid(BLOCK_SHORT_GRASS));
    assert(!block_is_solid(BLOCK_DANDELION));
    assert(block_drop_item(BLOCK_SUGAR_CANE, BLOCK_AIR) == ITEM_SUGAR_CANE);
    assert(block_drop_item(BLOCK_DANDELION, BLOCK_AIR) == BLOCK_DANDELION);

    /* 4. Terrain Flora Generation */
    World w;
    world_init(&w, 54321, 2);
    world_preload(&w, 0, 0, 1);
    bool found_flora = false;
    for (int x = -16; x < 16; x++) {
        for (int z = -16; z < 16; z++) {
            for (int y = 50; y < 90; y++) {
                uint8_t b = world_get_block(&w, x, y, z);
                if (b == BLOCK_SHORT_GRASS || b == BLOCK_DANDELION ||
                    b == BLOCK_POPPY || b == BLOCK_SUGAR_CANE) {
                    found_flora = true;
                    break;
                }
            }
            if (found_flora) break;
        }
        if (found_flora) break;
    }
    assert(found_flora);
    world_destroy(&w);
    printf("PASS: test_flora_generation_and_armor\n");
}

int main(void) {
    printf("Running Minecraft C Edition Verification Tests...\n");
    test_creative_invulnerability();
    test_chat_system();
    test_sky_cycle();
    test_spatial_hash_and_chunks();
    test_render_distance_and_fog();
    test_settings_persistence();
    test_ui_screens();
    test_inventory_and_crafting();
    test_survival_mechanics();
    test_tools_and_mining();
    test_crafting_3x3();
    test_furnace_smelting();
    test_food_and_eating();
    test_chest_mechanics();
    test_liquid_physics_and_buckets();
    test_slabs_and_stairs();
    test_redstone_mechanics_and_doors();
    test_arrow_projectile_physics();
    test_flora_generation_and_armor();
    printf("All verification tests passed successfully!\n");
    return 0;
}

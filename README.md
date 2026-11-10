Minecraft C Edition

A small Minecraft-like voxel sandbox written in C11. It uses SDL2 for windowing, OpenGL for rendering with an optional TinyGL software fallback, and includes textures and sounds directly in the repository without requiring a local Minecraft installation.

The game generates an endless voxel world with biomes, caves, ores, trees, water and lava. It supports creative and survival modes, mining and placing blocks, a hotbar and inventory, crafting, hunger, health, armor, TNT, a day and night cycle, chat commands, and local saves. It is not a complete Minecraft implementation.

Dependencies

A C11 compiler, CMake 3.16 or newer, SDL2 development files, OpenGL development files, and optionally libvorbisfile, libvorbis and libogg for sound. No Minecraft installation is required.

Building

Build the project:

cmake -S . -B build
cmake --build build -j

The default build uses hardware OpenGL. For the software renderer use:

cmake -S . -B build -DMCED_USE_GL=OFF
cmake --build build -j

Running

Start the game with:

./build/mced

Useful options include --width and --height, --fullscreen, --render-distance N, --play to skip the title screen and load the last world, --seed N, and --no-audio. Run ./build/mced --help for the full list.

Controls

Mouse looks around. W A S D move. Space jumps or swims up. Shift sneaks or flies down. Ctrl sprints. F toggles flight. F4 switches between creative and survival. E opens the inventory. T opens chat and / opens the command prompt. Left click mines and right click places a block. The number keys and mouse wheel select hotbar slots. Esc pauses, F3 shows debug information, F2 saves a screenshot, F11 toggles fullscreen, and M mutes sound.

Saves

World data is stored in saves/world.mced and player data in saves/player.dat. The world is regenerated from its seed, so only edited blocks are saved. Saves are written atomically and are autosaved every 30 seconds and on exit.

Legal

Minecraft assets are the property of Mojang Studios and Microsoft. This project is not affiliated with or endorsed by Mojang or Microsoft.

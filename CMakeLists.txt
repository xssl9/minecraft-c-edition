cmake_minimum_required(VERSION 3.16)
project(minecraft_c_edition C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MCED_USE_GL "Use hardware accelerated OpenGL" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

find_library(VORBISFILE_LIB vorbisfile)
find_library(VORBIS_LIB vorbis)
find_library(OGG_LIB ogg)

if(MCED_USE_GL)
  find_package(OpenGL REQUIRED)
else()
  set(TINYGL_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(TINYGL_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(TINYGL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/tinygl EXCLUDE_FROM_ALL)
endif()

add_executable(mced
  src/main.c
  src/assets.c
  src/blocks.c
  src/noise.c
  src/world.c
  src/render.c
  src/player.c
  src/ui.c
  src/audio.c
  src/settings.c
  src/entity.c
)

target_include_directories(mced PRIVATE
  src
  third_party
)

if(MCED_USE_GL)
  target_compile_definitions(mced PRIVATE MCED_USE_GL=1)
  target_link_libraries(mced PRIVATE OpenGL::GL PkgConfig::SDL2 m)
else()
  target_compile_definitions(mced PRIVATE MCED_USE_GL=0)
  target_include_directories(mced PRIVATE third_party/tinygl/include)
  target_link_libraries(mced PRIVATE tinygl-static PkgConfig::SDL2 m)
endif()

target_compile_options(mced PRIVATE -O3 -march=native -Wall -Wextra -Wno-unused-parameter)

if(VORBISFILE_LIB AND VORBIS_LIB AND OGG_LIB)
  target_compile_definitions(mced PRIVATE MCED_HAVE_VORBIS=1)
  target_link_libraries(mced PRIVATE ${VORBISFILE_LIB} ${VORBIS_LIB} ${OGG_LIB})
else()
  message(STATUS "libvorbisfile not found; audio disabled")
endif()

add_executable(verify_all
  tools/verify_all.c
  src/assets.c
  src/blocks.c
  src/noise.c
  src/world.c
  src/render.c
  src/player.c
  src/ui.c
  src/audio.c
  src/settings.c
  src/entity.c
)
target_include_directories(verify_all PRIVATE src third_party)
if(MCED_USE_GL)
  target_compile_definitions(verify_all PRIVATE MCED_USE_GL=1)
  target_link_libraries(verify_all PRIVATE OpenGL::GL PkgConfig::SDL2 m)
endif()
if(VORBISFILE_LIB AND VORBIS_LIB AND OGG_LIB)
  target_compile_definitions(verify_all PRIVATE MCED_HAVE_VORBIS=1)
  target_link_libraries(verify_all PRIVATE ${VORBISFILE_LIB} ${VORBIS_LIB} ${OGG_LIB})
endif()

# Keep the game runnable from the build directory as well as the source root.
add_custom_command(TARGET mced POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets
  COMMENT "Linking assets into the build directory")

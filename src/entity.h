#ifndef MCED_ENTITY_H
#define MCED_ENTITY_H

#include "assets.h"
#include "audio.h"
#include "mced.h"
#include "player.h"
#include "world.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENTITY_NONE = 0,
    ENTITY_ARROW
} EntityType;

typedef struct {
    int id;
    EntityType type;
    Vec3 pos;
    Vec3 vel;
    float yaw, pitch;
    float health;
    float max_health;
    float hurt_timer;
    float attack_cooldown;
    float wander_timer;
    float wander_yaw;
    bool on_ground;
    bool active;
    float width, height;
} Entity;

#define MAX_ENTITIES 128

typedef struct {
    Entity list[MAX_ENTITIES];
    int count;
    int next_id;
} EntityManager;

void entity_manager_init(EntityManager *em, const Assets *assets);
Entity *entity_spawn(EntityManager *em, EntityType type, Vec3 pos);
void entity_update_all(EntityManager *em, World *w, Player *p, Audio *audio, float dt, float world_time);
Entity *entity_raycast(EntityManager *em, Vec3 origin, Vec3 dir, float reach);
bool entity_damage(EntityManager *em, Entity *e, float damage, Vec3 knockback, Audio *audio);
void entity_render_all(const EntityManager *em, const Assets *assets, const Vec3 *player_pos);

#endif

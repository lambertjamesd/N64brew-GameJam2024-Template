#include "./building.h"

#include <libdragon.h>
#include <n64sys.h>
#include "./assets.h"
#include "./util/entity_id.h"
#include "./collision/box.h"
#include "./collision/collision_scene.h"
#include "./math/mathf.h"

#include "./rampage.h"

#define BUILDING_COLLIDE_GROUP 1
#define BUILDING_HEALTH 2

#define SHAKE_TIME          0.5f
#define SHAKE_AMPLITUDE     SCALE_FIXED_POINT(0.03f)

#define COLLAPSE_SPEED      SCALE_FIXED_POINT(0.5f)

#define HOUSE_SEGMENT_HEIGHT    SCALE_FIXED_POINT(1.0f)

static T3DMat4FP offsetMatrix[9];

struct dynamic_object_type building_colliders[] = {
    {
        .minkowsi_sum = box_minkowski_sum,
        .bounding_box = box_bounding_box,
        .data = {
            .box = {
                .half_size = {
                    SCALE_FIXED_POINT(0.5f),
                    SCALE_FIXED_POINT(0.5f), 
                    SCALE_FIXED_POINT(0.5f)
                },
            },
        },
        .bounce = 0.0f,
        .friction = 0.5f,
    },
    {
        .minkowsi_sum = box_minkowski_sum,
        .bounding_box = box_bounding_box,
        .data = {
            .box = {
                .half_size = {
                    SCALE_FIXED_POINT(0.5f),
                    SCALE_FIXED_POINT(1.0f), 
                    SCALE_FIXED_POINT(0.5f)
                },
            },
        },
        .bounce = 0.0f,
        .friction = 0.5f,
    },
    {
        .minkowsi_sum = box_minkowski_sum,
        .bounding_box = box_bounding_box,
        .data = {
            .box = {
                .half_size = {
                    SCALE_FIXED_POINT(0.5f),
                    SCALE_FIXED_POINT(1.5f), 
                    SCALE_FIXED_POINT(0.5f)
                },
            },
        },
        .bounce = 0.0f,
        .friction = 0.5f,
    },
    {
        .minkowsi_sum = box_minkowski_sum,
        .bounding_box = box_bounding_box,
        .data = {
            .box = {
                .half_size = {
                    SCALE_FIXED_POINT(0.5f),
                    SCALE_FIXED_POINT(2.0f), 
                    SCALE_FIXED_POINT(0.5f)
                },
            },
        },
        .bounce = 0.0f,
        .friction = 0.5f,
    },
};

void rampage_building_damage(void* data, int amount) {
    struct RampageBuilding* building = (struct RampageBuilding*)data;

    if (building->hp == 0) {
        return;
    }

    building->hp -= amount;
    building->shake_timer = SHAKE_TIME;

    if (building->hp <= 0) {
        building->health.is_dead = 1;
        building->is_collapsing = true;
    }
}

void rampage_building_init(struct RampageBuilding* building, T3DVec3* position) {
    int entity_id = entity_id_next();

    float random_height = randomInRangef(0.0f, 1.0f);

    if (random_height < 0.125f) {
        building->height = 1;
    } else if (random_height < 0.75f) {
        building->height = 2;
    } else if (random_height < 0.95) {
        building->height = 3;
    } else {
        building->height = 4;
    }

    dynamic_object_init(
        entity_id,
        &building->dynamic_object,
        &building_colliders[building->height - 1],
        COLLISION_LAYER_TANGIBLE,
        (struct Vector3*)position,
        &gRight2
    );

    building->dynamic_object.collision_group = BUILDING_COLLIDE_GROUP;
    building->dynamic_object.center.y = building_colliders[building->height].data.box.half_size.y;
    building->dynamic_object.is_fixed = true;
    building->is_destroyed = false;

    building->hp = building->height * BUILDING_HEALTH;
    building->shake_timer = 0.0f;

    collision_scene_add(&building->dynamic_object);

    health_register(entity_id, &building->health, rampage_building_damage, building);

    for (int i = 0; i < 9; i += 1) {
        int x = i % 3;
        int y = i / 3;

        T3DVec3 pos;
        pos.v[0] = (x - 1) * SHAKE_AMPLITUDE;
        pos.v[1] = HOUSE_SEGMENT_HEIGHT;
        pos.v[2] = (y - 1) * SHAKE_AMPLITUDE;
        t3d_mat4fp_identity(UncachedAddr(&offsetMatrix[i]));
        t3d_mat4fp_set_pos(UncachedAddr(&offsetMatrix[i]), pos.v);
    }
}

void rampage_building_destroy(struct RampageBuilding* building) {
    if (building->is_destroyed) {
        return;
    }
    collision_scene_remove(&building->dynamic_object);
    health_unregister(building->dynamic_object.entity_id);
    building->is_destroyed = true;
}

void rampage_building_render(struct RampageBuilding* building) {
    if (building->is_destroyed) {
        return;
    }

    T3DQuat rotation;
    t3d_quat_identity(&rotation);
    T3DVec3 scale = {{1.0f, 1.0f, 1.0f}};

    struct Vector3 final_pos = building->dynamic_object.position;
    
    if (building->is_collapsing) {
        final_pos.x += randomInRangef(-SHAKE_AMPLITUDE, SHAKE_AMPLITUDE);
        final_pos.z += randomInRangef(-SHAKE_AMPLITUDE, SHAKE_AMPLITUDE);
    } else if (building->shake_timer > 0.0f) {
        float amplitude = (SHAKE_AMPLITUDE * (1.0f / SHAKE_TIME)) * building->shake_timer;

        final_pos.x += randomInRangef(-amplitude, amplitude);
        final_pos.z += randomInRangef(-amplitude, amplitude);
    }

    t3d_mat4fp_from_srt(UncachedAddr(&building->mtx), scale.v, rotation.v, (float*)&final_pos);
    t3d_matrix_push(&building->mtx);
    rspq_block_run(rampage_assets_get()->buildingSplit.mesh);

    for (int i = 1; i < building->height; i += 1) {
        t3d_matrix_push(&offsetMatrix[building->is_collapsing ? randomInRange(0, 9) : 5]);
        rspq_block_run(rampage_assets_get()->buildingSplit.mesh);
    }
    
    t3d_matrix_pop(building->height);
}

void rampage_building_update(struct RampageBuilding* building, float delta_time) {
    if (building->is_destroyed) {
        return;
    }

    if (building->shake_timer > 0.0f) {
        building->shake_timer -= delta_time;
    }

    if (building->is_collapsing) {
        building->dynamic_object.position.y -= COLLAPSE_SPEED * delta_time;

        if (building->dynamic_object.bounding_box.max.y < 0.0f) {
            rampage_building_destroy(building);
        }
    }
}